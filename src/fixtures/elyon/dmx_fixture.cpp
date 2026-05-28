#ifdef RAVLIGHT_FIXTURE_ELYON
#include "fixtures/elyon/dmx_fixture.h"
#include "fixtures/elyon/fixture.h"
#include "core/led_output.h"
#include "core/i2s_parallel_output.h"
#include "pwm_output.h"
#include "dmx_manager.h"
#include "config.h"
#include "esp_log.h"
#include <driver/gpio.h>
#include <string.h>
#include <stdlib.h>

static const char* TAG = "ELYON";

bool handleDMXenable = true;

// ── Per-output state ──────────────────────────────────────────────────────────

// WS/SK pixel strips — buf + metadata only; RMT is not used.
// led_output_init() is intentionally NOT called; we manage buf ourselves.
static led_output_t strips[HW_LED_OUTPUT_COUNT];
static bool         stripActive[HW_LED_OUTPUT_COUNT];

static pwm_output_t pwms[HW_LED_OUTPUT_COUNT];
static bool         pwmActive[HW_LED_OUTPUT_COUNT];

static bool         relayActive[HW_LED_OUTPUT_COUNT];

// Set to true once i2s_par_init() has been called for this run
static bool         i2sInitDone = false;

// ── Helpers ───────────────────────────────────────────────────────────────────

static inline bool proto_is_ws(led_protocol_t p) {
    return p == LED_WS2811 || p == LED_WS2812B ||
           p == LED_SK6812  || p == LED_WS2814;
}

// ── Public API ────────────────────────────────────────────────────────────────

void initFixture() {
    // Register universes needed by all active outputs
    for (int i = 0; i < ELYON_NUM_OUTPUTS; i++) {
        const led_output_cfg_t& cfg = elyonConfig.outputs[i];
        uint8_t n = led_universe_count(&cfg);
        for (uint8_t u = 0; u < n; u++)
            registerDmxUniverse(cfg.universe_start + u);
    }

    // ── Pre-scan: build I2S config from all WS/SK outputs ────────────────────
    i2s_par_cfg_t i2s_cfg = {};
    i2s_cfg.n_channels        = HW_LED_OUTPUT_COUNT;
    i2s_cfg.max_pixels_per_ch = ELYON_MAX_PIXELS_PER_OUT;
    for (int i = 0; i < HW_LED_OUTPUT_COUNT; i++)
        i2s_cfg.gpio_pins[i] = -1;  // default: unused

    bool has_ws = false;
    uint32_t totalPixels = 0;
    for (int i = 0; i < ELYON_NUM_OUTPUTS; i++) {
        const led_output_cfg_t& cfg = elyonConfig.outputs[i];
        if (!proto_is_ws(cfg.protocol) || cfg.pixel_count == 0) continue;
        totalPixels += cfg.pixel_count;
        if (totalPixels > ELYON_MAX_PIXELS_TOTAL) continue;
        i2s_cfg.gpio_pins[i] = HW_LED_OUTPUT_PINS[i];
        has_ws = true;
    }

    if (has_ws) {
        esp_err_t err = i2s_par_init(&i2s_cfg);
        if (err == ESP_OK) i2sInitDone = true;
        else ESP_LOGE(TAG, "i2s_par_init failed: %d", err);
    }

    // ── Per-output init ───────────────────────────────────────────────────────
    totalPixels = 0;
    for (int i = 0; i < ELYON_NUM_OUTPUTS; i++) {
        stripActive[i]  = false;
        pwmActive[i]    = false;
        relayActive[i]  = false;
        const led_output_cfg_t& cfg = elyonConfig.outputs[i];
        int pin = HW_LED_OUTPUT_PINS[i];

        if (cfg.protocol == LED_RELAY) {
            gpio_config_t gc = {};
            gc.pin_bit_mask  = 1ULL << pin;
            gc.mode          = GPIO_MODE_OUTPUT;
            gc.pull_up_en    = GPIO_PULLUP_DISABLE;
            gc.pull_down_en  = GPIO_PULLDOWN_DISABLE;
            gc.intr_type     = GPIO_INTR_DISABLE;
            gpio_config(&gc);
            gpio_set_level((gpio_num_t)pin, 0);
            relayActive[i] = true;
            ESP_LOGI(TAG, "ch%d gpio%d RELAY threshold=%u univ=%d ch=%d",
                     i, pin, cfg.relay_threshold, cfg.universe_start, cfg.dmx_start);
            continue;
        }

        if (cfg.protocol == LED_PWM) {
            if (cfg.pwm_freq_hz == 0) continue;
            if (pwm_output_init(&pwms[i], pin, (uint8_t)i, cfg.pwm_freq_hz)) {
                pwmActive[i] = true;
                static const char* curves[] = {"linear", "quadratic", "cubic"};
                ESP_LOGI(TAG, "ch%d gpio%d PWM %uHz %s %s%s",
                         i, pin, (unsigned)cfg.pwm_freq_hz,
                         curves[cfg.pwm_curve < 3 ? cfg.pwm_curve : 0],
                         cfg.pwm_16bit ? "16bit" : "8bit",
                         cfg.pwm_invert ? " inv" : "");
            }
            continue;
        }

        if (!proto_is_ws(cfg.protocol) || cfg.pixel_count == 0) continue;

        totalPixels += cfg.pixel_count;
        if (totalPixels > ELYON_MAX_PIXELS_TOTAL) {
            ESP_LOGW(TAG, "ch%d skipped: total pixel budget exceeded (%lu > %d)",
                     i, (unsigned long)totalPixels, ELYON_MAX_PIXELS_TOTAL);
            continue;
        }

        uint8_t ch_pp = led_ch_per_pixel(cfg.protocol);

        // Allocate pixel buffer; no RMT driver installed — I2S handles output.
        strips[i].n_pixels = cfg.pixel_count;
        strips[i].channels = ch_pp;
        strips[i].buf      = (uint8_t*)calloc((size_t)cfg.pixel_count * ch_pp, 1);
        if (!strips[i].buf) {
            ESP_LOGE(TAG, "ch%d buf alloc failed", i);
            continue;
        }

        if (i2sInitDone)
            i2s_par_set_source((uint8_t)i, strips[i].buf, cfg.pixel_count, ch_pp);

        stripActive[i] = true;
        char order_str[5];
        color_order_to_str(cfg.color_order, ch_pp, order_str);
        ESP_LOGI(TAG, "ch%d gpio%d n=%d proto=%d order=%s univ=%d ch=%d group=%d inv=%d bri=%d",
                 i, pin, cfg.pixel_count, cfg.protocol, order_str,
                 cfg.universe_start, cfg.dmx_start,
                 cfg.grouping, cfg.invert, cfg.brightness);
    }
}

// Multi-universe renderer.
// Pixel decoding runs under dmxBufferMutex; DMA runs after the mutex is released
// so ArtNet/sACN writes to the universe pool are not blocked during transmission.
void handleDMX() {
    if (!handleDMXenable) return;

    xSemaphoreTake(dmxBufferMutex, portMAX_DELAY);

    bool any_ws = false;

    for (int i = 0; i < ELYON_NUM_OUTPUTS; i++) {
        const led_output_cfg_t& cfg = elyonConfig.outputs[i];

        // ── Relay ─────────────────────────────────────────────────────────────
        if (relayActive[i]) {
            const uint8_t* ubuf = getUniverseData(cfg.universe_start);
            if (ubuf) {
                uint8_t val = ubuf[cfg.dmx_start];
                gpio_set_level((gpio_num_t)HW_LED_OUTPUT_PINS[i],
                               val >= cfg.relay_threshold ? 1 : 0);
            }
            continue;
        }

        // ── PWM ───────────────────────────────────────────────────────────────
        if (pwmActive[i]) {
            const uint8_t* ubuf = getUniverseData(cfg.universe_start);
            if (ubuf) {
                uint16_t ch_idx = cfg.dmx_start - 1;
                if (cfg.pwm_16bit) {
                    uint16_t msb = ubuf[ch_idx + 1];
                    uint16_t lsb = (ch_idx + 1 < 512) ? ubuf[ch_idx + 2] : 0;
                    uint16_t val = (msb << 8) | lsb;
                    val = (uint16_t)((uint32_t)val * cfg.brightness / 255);
                    if (cfg.pwm_invert) val = 65535u - val;
                    pwm_output_set16(&pwms[i], val, cfg.pwm_curve);
                } else {
                    uint8_t val = ubuf[ch_idx + 1];
                    val = (uint8_t)((uint16_t)val * cfg.brightness / 255);
                    if (cfg.pwm_invert) val = 255u - val;
                    pwm_output_set(&pwms[i], val, cfg.pwm_curve);
                }
            }
            continue;
        }

        // ── WS / SK pixel strip ───────────────────────────────────────────────
        if (!stripActive[i]) continue;
        uint8_t ch_pp = led_ch_per_pixel(cfg.protocol);

        for (uint16_t px = 0; px < cfg.pixel_count; px++) {
            uint32_t slot     = px / cfg.grouping;
            uint32_t flat     = (uint32_t)(cfg.dmx_start - 1) + slot * ch_pp;
            uint16_t universe = cfg.universe_start + (uint16_t)(flat / 512);
            uint16_t ch_idx   = (uint16_t)(flat % 512);

            const uint8_t* ubuf = getUniverseData(universe);
            if (!ubuf || ch_idx + ch_pp > 512) break;

            uint8_t logical[4] = {0, 0, 0, 0};
            for (uint8_t c = 0; c < ch_pp; c++)
                logical[c] = (uint16_t)ubuf[ch_idx + 1 + c] * cfg.brightness / 255;

            uint8_t wire[4];
            for (uint8_t c = 0; c < ch_pp; c++)
                wire[c] = logical[cfg.color_order[c] & 3];

            uint16_t out_idx = cfg.invert ? (cfg.pixel_count - 1 - px) : px;
            led_output_write_raw(&strips[i], out_idx, wire);
        }
        any_ws = true;
    }

    // Release universe pool mutex before DMA so ArtNet/sACN is not blocked
    // during the ~30 ms transmission window.
    xSemaphoreGive(dmxBufferMutex);

    if (any_ws && i2sInitDone) {
        i2s_par_trigger_frame();
        i2s_par_wait_done();
    }
}

void fixtureHighlight() {}

void startDMX() {
    handleDMXenable = true;
}

void stopDMX() {
    handleDMXenable = false;

    // Clear all pixel buffers and send one blank I2S frame
    bool any_ws = false;
    for (int i = 0; i < ELYON_NUM_OUTPUTS; i++) {
        if (stripActive[i]) {
            led_output_clear(&strips[i]);
            any_ws = true;
        }
        if (pwmActive[i]) {
            uint8_t off = elyonConfig.outputs[i].pwm_invert ? 255 : 0;
            pwm_output_set(&pwms[i], off, 0);
        }
        if (relayActive[i]) {
            gpio_set_level((gpio_num_t)HW_LED_OUTPUT_PINS[i], 0);
        }
    }

    if (any_ws && i2sInitDone) {
        i2s_par_trigger_frame();
        i2s_par_wait_done();
    }
}

#endif // RAVLIGHT_FIXTURE_ELYON
