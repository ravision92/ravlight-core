#ifdef RAVLIGHT_FIXTURE_ELYON
#include "fixtures/elyon/dmx_fixture.h"
#include "fixtures/elyon/fixture.h"
#include "core/led_output.h"
#include "pwm_output.h"
#include "dmx_manager.h"
#include "config.h"
#include "esp_log.h"
#include <driver/gpio.h>
#include <string.h>

static const char* TAG = "ELYON";

bool handleDMXenable = true;

// Board GPIO map — index 0..N via HW_LED_OUTPUT_PINS[] defined in the board file.

// One RMT channel per output; mem_blocks=1 (up to 8 RMT channels available on ESP32).
static led_output_t strips[HW_LED_OUTPUT_COUNT];
static bool         stripActive[HW_LED_OUTPUT_COUNT];

// LEDC PWM outputs (LED_PWM protocol).
// channel_idx < 8 → LEDC_LOW_SPEED_MODE; channel_idx 8–15 → LEDC_HIGH_SPEED_MODE.
static pwm_output_t pwms[HW_LED_OUTPUT_COUNT];
static bool         pwmActive[HW_LED_OUTPUT_COUNT];

// GPIO relay outputs (LED_RELAY protocol).
static bool relayActive[HW_LED_OUTPUT_COUNT];

// ── Public API ────────────────────────────────────────────────────────────────

void initFixture() {
    // Register all universes needed by active outputs
    for (int i = 0; i < ELYON_NUM_OUTPUTS; i++) {
        const elyon_output_cfg_t& cfg = elyonConfig.outputs[i];
        uint8_t n = elyon_universe_count(&cfg);
        for (uint8_t u = 0; u < n; u++)
            registerDmxUniverse(cfg.universe_start + u);
    }

    uint32_t totalPixels = 0;
    for (int i = 0; i < ELYON_NUM_OUTPUTS; i++) {
        stripActive[i]  = false;
        pwmActive[i]    = false;
        relayActive[i]  = false;
        const elyon_output_cfg_t& cfg = elyonConfig.outputs[i];
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

        if (cfg.pixel_count == 0) continue;

        totalPixels += cfg.pixel_count;
        if (totalPixels > ELYON_MAX_PIXELS_TOTAL) {
            ESP_LOGW(TAG, "ch%d skipped: total pixel budget exceeded (%lu > %d)",
                     i, (unsigned long)totalPixels, ELYON_MAX_PIXELS_TOTAL);
            continue;
        }

        uint8_t ch_pp = led_ch_per_pixel(cfg.protocol);
        esp_err_t err = led_output_init(&strips[i], pin, cfg.pixel_count, (rmt_channel_t)i, 1, ch_pp);
        if (err == ESP_OK) {
            stripActive[i] = true;
            char order_str[5];
            color_order_to_str(cfg.color_order, ch_pp, order_str);
            ESP_LOGI(TAG, "ch%d gpio%d n=%d proto=%d order=%s univ=%d ch=%d group=%d inv=%d bri=%d",
                     i, pin, cfg.pixel_count, cfg.protocol, order_str,
                     cfg.universe_start, cfg.dmx_start,
                     cfg.grouping, cfg.invert, cfg.brightness);
        } else {
            ESP_LOGE(TAG, "ch%d init failed err=%d", i, err);
        }
    }
}

// Multi-universe renderer using flat channel index math.
// For each pixel, the flat channel address spans universe boundaries:
//   flat     = (dmx_start - 1) + slot * ch_per_pixel   (0-based)
//   universe = universe_start + flat / 512
//   ch_idx   = flat % 512                               (0-based within universe)
//   pool buf is 1-indexed: buf[ch_idx + 1] = channel ch_idx+1
void handleDMX() {
    if (!handleDMXenable) return;

    xSemaphoreTake(dmxBufferMutex, portMAX_DELAY);

    for (int i = 0; i < ELYON_NUM_OUTPUTS; i++) {
        const elyon_output_cfg_t& cfg = elyonConfig.outputs[i];

        // ── Relay branch ─────────────────────────────────────────────────────────
        if (relayActive[i]) {
            const uint8_t* ubuf = getUniverseData(cfg.universe_start);
            if (ubuf) {
                uint8_t val = ubuf[cfg.dmx_start];  // 1-indexed buffer
                gpio_set_level((gpio_num_t)HW_LED_OUTPUT_PINS[i],
                               val >= cfg.relay_threshold ? 1 : 0);
            }
            continue;
        }

        // ── PWM branch ──────────────────────────────────────────────────────────
        if (pwmActive[i]) {
            const uint8_t* ubuf = getUniverseData(cfg.universe_start);
            if (ubuf) {
                uint16_t ch_idx = cfg.dmx_start - 1;  // 0-based within universe
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

        if (!stripActive[i]) continue;
        uint8_t ch_pp = led_ch_per_pixel(cfg.protocol);

        for (uint16_t px = 0; px < cfg.pixel_count; px++) {
            uint32_t slot     = px / cfg.grouping;
            uint32_t flat     = (uint32_t)(cfg.dmx_start - 1) + slot * ch_pp;
            uint16_t universe = cfg.universe_start + (uint16_t)(flat / 512);
            uint16_t ch_idx   = (uint16_t)(flat % 512);   // 0-based within universe

            const uint8_t* ubuf = getUniverseData(universe);
            if (!ubuf || ch_idx + ch_pp > 512) break;

            // Read logical channels from DMX (always in RGBW order from universe buffer).
            // buf is 1-indexed; ch_idx is 0-based → ubuf[ch_idx + 1] = first channel.
            uint8_t logical[4] = {0, 0, 0, 0};
            for (uint8_t c = 0; c < ch_pp; c++)
                logical[c] = (uint16_t)ubuf[ch_idx + 1 + c] * cfg.brightness / 255;

            // Apply color order: wire[i] = logical[color_order[i]]
            uint8_t wire[4];
            for (uint8_t c = 0; c < ch_pp; c++)
                wire[c] = logical[cfg.color_order[c] & 3];

            uint16_t out_idx = cfg.invert ? (cfg.pixel_count - 1 - px) : px;
            led_output_write_raw(&strips[i], out_idx, wire);
        }
        led_output_flush(&strips[i]);
    }

    xSemaphoreGive(dmxBufferMutex);
}

void fixtureHighlight() {}

void startDMX() {
    handleDMXenable = true;
}

void stopDMX() {
    handleDMXenable = false;
    for (int i = 0; i < ELYON_NUM_OUTPUTS; i++) {
        if (stripActive[i]) {
            led_output_clear(&strips[i]);
            led_output_flush(&strips[i]);
        }
        if (pwmActive[i]) {
            uint8_t off = elyonConfig.outputs[i].pwm_invert ? 255 : 0;
            pwm_output_set(&pwms[i], off, 0);
        }
        if (relayActive[i]) {
            gpio_set_level((gpio_num_t)HW_LED_OUTPUT_PINS[i], 0);
        }
    }
}

#endif // RAVLIGHT_FIXTURE_ELYON
