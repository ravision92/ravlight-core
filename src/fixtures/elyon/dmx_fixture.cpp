#ifdef RAVLIGHT_FIXTURE_ELYON
#include "fixtures/elyon/dmx_fixture.h"
#include "fixtures/elyon/fixture.h"
#include "core/led_output.h"
#include "core/clocked_output.h"
#include "pwm_output.h"
#include "dmx_manager.h"
#include "config.h"
#include "core/stats.h"
#include "esp_log.h"
#include <driver/gpio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#ifdef RAVLIGHT_MODULE_I2S_LED
#include "core/i2s_parallel_output.h"
#include <stdlib.h>
#endif

static const char* TAG = "ELYON";

bool handleDMXenable = true;

// ── Per-output brightness LUT (Phase 3 Stage B) ─────────────────────────────
// Pre-applied brightness pulls the per-channel multiply-divide out of the
// render hot path:
//   before: wire[c] = ubuf[ch+c] * cfg.brightness / 255    // 5-8 cycles + div
//   after : wire[c] = s_bright_lut[i][ubuf[ch+c]]          // 1 load
// Saves a few cycles per channel; on 8 outputs × 1024 px × 4 ch × 50 fps that
// is ~6.5 M ops/s of multiply-divide replaced by L1-cached lookups.
// Rebuilt by rebuildBrightnessLuts() from initFixture() and after every
// fixtureApplyLive() that touches a brightness value.
static uint8_t s_bright_lut[HW_LED_OUTPUT_COUNT][256];

static void rebuildBrightnessLuts() {
    for (int i = 0; i < HW_LED_OUTPUT_COUNT; i++) {
        const uint16_t b = elyonConfig.outputs[i].brightness;
        for (int v = 0; v < 256; v++)
            s_bright_lut[i][v] = (uint8_t)((uint32_t)v * b / 255);
    }
}

// ── Dual-core render task ───────────────────────────────────────────────────
// Rendering runs in a dedicated task pinned to Core 0, while ArtNet/sACN
// receive (AsyncUDP) and the Arduino main loop stay on Core 1. Two benefits:
//   • Main loop no longer burns Core 1 cycles re-rendering on every iteration
//     (thousands of times/sec the same frame); render now triggers on a
//     notification from the network ingest callback, or a 20 ms safety tick.
//   • Mutex contention shrinks — ArtNet writes to the universe pool on Core 1
//     and the render task reads it on Core 0; both still take dmxBufferMutex
//     but only for the brief moments they actually touch the pool.
// The render body itself is unchanged (elyon_render_impl); only the
// dispatch (handleDMX) is now asynchronous.

static TaskHandle_t s_render_task = NULL;
static void elyon_render_impl();

static void elyon_render_task_fn(void* arg) {
    (void)arg;
    for (;;) {
        // Block on notify, or wake every 20 ms (50 fps minimum keep-alive
        // so highlight wipes and the DMX-loss watchdog tick even when no
        // ArtNet frames are arriving).
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(20));
        if (handleDMXenable) elyon_render_impl();
    }
}

static void elyon_render_task_start() {
    if (s_render_task) return;
    // Stack: 6 KB covers the render loop + the 256 b on-stack pixel buffers.
    // Priority 5 sits below AsyncUDP (~7) so the network ingest task always
    // wins when both are ready. Pinned to Core 0 (PRO_CPU); APP_CPU stays
    // free for the Arduino loop, web server, OTA, sensors.
    xTaskCreatePinnedToCore(elyon_render_task_fn, "elyon_render",
                            6144, NULL, 5, &s_render_task, 0);
    ESP_LOGI(TAG, "render task pinned to Core 0 (handle=%p)", s_render_task);
}

// Public dispatch — main loop calls this every iteration. We just signal the
// render task so it runs once. ArtNet/sACN ingest callbacks can call this
// too (via the existing DMXLedRun -> notifyElyonRender hook, future work).
void handleDMX() {
    if (!s_render_task) return;
    xTaskNotifyGive(s_render_task);
}

// ── Per-output state (shared across both output backends) ───────────────────
static led_output_t strips[HW_LED_OUTPUT_COUNT];
static bool         stripActive[HW_LED_OUTPUT_COUNT];

static pwm_output_t pwms[HW_LED_OUTPUT_COUNT];
static bool         pwmActive[HW_LED_OUTPUT_COUNT];

static bool         relayActive[HW_LED_OUTPUT_COUNT];

// Per-output highlight wipe (identification). 0 = inactive, else millis() at start.
static uint32_t     hlStart[HW_LED_OUTPUT_COUNT] = {0};
#define ELYON_HL_MS 1500

// ── Public API: implementation switches at compile time ─────────────────────
// Default = RMT (one channel per output, validated path).
// RAVLIGHT_MODULE_I2S_LED = I2S parallel (single peripheral drives all 8 outputs;
//   contributed by Felix Trefou aka Lefix2 on his fork — branch
//   claude/epic-archimedes-4tTuP — lifts the per-output pixel cap from 500 to 1024).

#ifdef RAVLIGHT_MODULE_I2S_LED
// ─── Mix-capable path (per-output RMT, I2S, clocked, PWM, relay) ─────────────
// Each WS281x output picks its backend via cfg.backend (LED_BACKEND_RMT or _I2S).
// Outputs marked I2S are routed through the shared I2S0 parallel engine; the
// others run on their own RMT channel. Clocked outputs (APA102/SK9822/P9813)
// always use the bit-bang clocked_output driver regardless of backend. PWM and
// Relay are protocol-driven and ignore the backend field.

static clocked_output_t clockedStrips[HW_LED_OUTPUT_COUNT];
static bool             clockedActive[HW_LED_OUTPUT_COUNT];
static bool             i2sOwned[HW_LED_OUTPUT_COUNT];  // true → strips[i].buf is fed by I2S
static bool             i2sInitDone = false;

static inline bool proto_is_ws(led_protocol_t p) {
    return p == LED_WS2811 || p == LED_WS2812B ||
           p == LED_SK6812  || p == LED_WS2814  ||
           p == LED_WS2815  || p == LED_TM1814  ||
           p == LED_TM1914;
}

void initFixture() {
    // Register universes needed by all active outputs
    for (int i = 0; i < ELYON_NUM_OUTPUTS; i++) {
        const led_output_cfg_t& cfg = elyonConfig.outputs[i];
        uint8_t n = led_universe_count(&cfg);
        for (uint8_t u = 0; u < n; u++)
            registerDmxUniverse(cfg.universe_start + u);
    }

    // Pre-scan: build the I2S config from WS281x outputs whose backend is I2S.
    // Wire byte width is the MAX bytes-per-pixel across those outputs (I2S emits
    // one width to every channel; RGB strips on the same bus get W=0).
    i2s_par_cfg_t i2s_cfg = {};
    i2s_cfg.n_channels        = HW_LED_OUTPUT_COUNT;
    i2s_cfg.max_pixels_per_ch = ELYON_MAX_PIXELS_PER_OUT;
    i2s_cfg.bytes_pp          = 3;
    for (int i = 0; i < HW_LED_OUTPUT_COUNT; i++) i2s_cfg.gpio_pins[i] = -1;

    bool has_i2s = false;
    uint32_t prescanPixels = 0;
    for (int i = 0; i < ELYON_NUM_OUTPUTS; i++) {
        const led_output_cfg_t& cfg = elyonConfig.outputs[i];
        if (!proto_is_ws(cfg.protocol) || cfg.pixel_count == 0) continue;
        if (cfg.backend != (uint8_t)LED_BACKEND_I2S) continue;
        prescanPixels += cfg.pixel_count;
        if (prescanPixels > ELYON_MAX_PIXELS_TOTAL) continue;
        i2s_cfg.gpio_pins[i] = HW_LED_OUTPUT_PINS[i];
        uint8_t ch_pp = led_ch_per_pixel(cfg.protocol);
        if (ch_pp > i2s_cfg.bytes_pp) i2s_cfg.bytes_pp = ch_pp;
        has_i2s = true;
    }

    if (has_i2s) {
        esp_err_t err = i2s_par_init(&i2s_cfg);
        if (err == ESP_OK) i2sInitDone = true;
        else ESP_LOGE(TAG, "i2s_par_init failed: %d", err);
    }

    // Per-output init: dispatch by protocol, then by backend for WS281x.
    uint32_t totalPixels = 0;
    for (int i = 0; i < ELYON_NUM_OUTPUTS; i++) {
        stripActive[i]   = false;
        pwmActive[i]     = false;
        relayActive[i]   = false;
        clockedActive[i] = false;
        i2sOwned[i]      = false;
        const led_output_cfg_t& cfg = elyonConfig.outputs[i];
        int pin = HW_LED_OUTPUT_PINS[i];

        if (cfg.protocol == LED_CLOCK_FOLLOWER) continue;

        if (cfg.protocol == LED_RELAY) {
            gpio_config_t gc = {};
            gc.pin_bit_mask  = 1ULL << pin;
            gc.mode          = GPIO_MODE_OUTPUT;
            gc.pull_up_en    = GPIO_PULLUP_DISABLE;
            gc.pull_down_en  = GPIO_PULLDOWN_DISABLE;
            gc.intr_type     = GPIO_INTR_DISABLE;
            gpio_config(&gc);
            gpio_set_level((gpio_num_t)pin, cfg.relay_invert ? 1 : 0);
            relayActive[i] = true;
            ESP_LOGI(TAG, "ch%d gpio%d RELAY threshold=%u inv=%u univ=%d ch=%d",
                     i, pin, cfg.relay_threshold, cfg.relay_invert, cfg.universe_start, cfg.dmx_start);
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

        if (led_is_clocked(cfg.protocol)) {
            if (cfg.pixel_count == 0) continue;
            uint8_t partner = cfg.clock_partner_idx;
            if (partner >= HW_LED_OUTPUT_COUNT || partner == i) {
                ESP_LOGW(TAG, "ch%d clocked: invalid partner idx %u", i, partner);
                continue;
            }
            uint8_t ch_pp = led_ch_per_pixel(cfg.protocol);
            esp_err_t cerr = clocked_output_init(&clockedStrips[i], pin,
                                                  HW_LED_OUTPUT_PINS[partner],
                                                  cfg.pixel_count, ch_pp);
            if (cerr != ESP_OK) {
                ESP_LOGE(TAG, "ch%d clocked init failed: %d", i, cerr);
                continue;
            }
            clockedActive[i] = true;
            ESP_LOGI(TAG, "ch%d data=gpio%d clock=gpio%d n=%d proto=%d (clocked) univ=%d ch=%d",
                     i, pin, HW_LED_OUTPUT_PINS[partner], cfg.pixel_count,
                     cfg.protocol, cfg.universe_start, cfg.dmx_start);
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

        if (cfg.backend == (uint8_t)LED_BACKEND_I2S && i2sInitDone) {
            // I2S-owned: alloc DMA-friendly buffer, register it with the I2S driver.
            strips[i].n_pixels = cfg.pixel_count;
            strips[i].channels = ch_pp;
            strips[i].buf      = (uint8_t*)calloc((size_t)cfg.pixel_count * ch_pp, 1);
            if (!strips[i].buf) {
                ESP_LOGE(TAG, "ch%d buf alloc failed (I2S)", i);
                continue;
            }
            i2s_par_set_source((uint8_t)i, strips[i].buf, cfg.pixel_count, ch_pp);
            i2sOwned[i] = true;
        } else {
            // RMT-owned: led_output_init allocates the buffer internally and binds
            // RMT channel i. ESP32 classic has 8 RMT channels (0..7).
            esp_err_t err = led_output_init(&strips[i], pin, cfg.pixel_count,
                                            (rmt_channel_t)i, 1, ch_pp);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "ch%d led_output_init failed: %d", i, err);
                continue;
            }
        }

        stripActive[i] = true;
        char order_str[5];
        color_order_to_str(cfg.color_order, ch_pp, order_str);
        ESP_LOGI(TAG, "ch%d gpio%d n=%d proto=%d order=%s univ=%d ch=%d group=%d inv=%d bri=%d backend=%s",
                 i, pin, cfg.pixel_count, cfg.protocol, order_str,
                 cfg.universe_start, cfg.dmx_start,
                 cfg.grouping, cfg.invert, cfg.brightness,
                 i2sOwned[i] ? "I2S" : "RMT");
    }

    rebuildBrightnessLuts();
    elyon_render_task_start();
}

// Pixel decoding runs under dmxBufferMutex; DMA runs after the mutex is released
// so ArtNet/sACN writes to the universe pool are not blocked during transmission.
// Runs on the dedicated render task (Core 0) — see elyon_render_task_fn above.
static void elyon_render_impl() {
    if (!dmxBufferMutex) return;  // guard: mutex created in initDmxInputs(), which runs after initFixture()
    stats_render_frame_start();
    stats_render_mutex_wait_start();
    xSemaphoreTake(dmxBufferMutex, portMAX_DELAY);
    stats_render_mutex_wait_end();

    bool any_i2s = false;

    for (int i = 0; i < ELYON_NUM_OUTPUTS; i++) {
        const led_output_cfg_t& cfg = elyonConfig.outputs[i];

        if (relayActive[i]) {
            const uint8_t* ubuf = getUniverseData(cfg.universe_start);
            if (ubuf) {
                uint8_t val = ubuf[cfg.dmx_start];
                bool on = val >= cfg.relay_threshold;
                if (cfg.relay_invert) on = !on;
                gpio_set_level((gpio_num_t)HW_LED_OUTPUT_PINS[i], on ? 1 : 0);
            }
            continue;
        }

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

        // Clocked outputs (always bit-bang regardless of backend setting).
        if (clockedActive[i]) {
            uint8_t ch_pp_c = led_ch_per_pixel(cfg.protocol);
            const uint8_t* cached_ubuf = nullptr;
            uint16_t       cached_univ = 0xFFFF;

            for (uint16_t px = 0; px < cfg.pixel_count; px++) {
                uint32_t slot = px / cfg.grouping;
                uint8_t  logical[4] = {0, 0, 0, 0};
                bool     skip = false;

                for (uint8_t c = 0; c < ch_pp_c; c++) {
                    uint32_t ch_flat   = (uint32_t)(cfg.dmx_start - 1) + slot * ch_pp_c + c;
                    uint16_t ch_univ   = cfg.universe_start + (uint16_t)(ch_flat / 512);
                    uint16_t ch_offset = (uint16_t)(ch_flat % 512);

                    if (ch_univ != cached_univ) {
                        cached_ubuf = getUniverseData(ch_univ);
                        cached_univ = ch_univ;
                    }
                    if (!cached_ubuf) { skip = true; break; }
                    logical[c] = s_bright_lut[i][cached_ubuf[ch_offset + 1]];
                }
                if (skip) continue;

                uint8_t wire[4];
                for (uint8_t c = 0; c < ch_pp_c; c++)
                    wire[c] = logical[cfg.color_order[c] & 3];

                uint16_t out_idx = cfg.invert ? (cfg.pixel_count - 1 - px) : px;
                clocked_output_write_raw(&clockedStrips[i], out_idx, wire);
            }
            continue;
        }

        if (!stripActive[i]) continue;
        uint8_t ch_pp = led_ch_per_pixel(cfg.protocol);

        // Highlight wipe (identification): a white band sweeps the strip.
        if (hlStart[i]) {
            uint32_t el = millis() - hlStart[i];
            if (el < ELYON_HL_MS) {
                uint16_t n = cfg.pixel_count;
                uint16_t head = (uint16_t)((uint32_t)el * n / ELYON_HL_MS);
                uint16_t band = n / 12; if (band < 1) band = 1;
                uint8_t on[4]  = {255, 255, 255, 255};
                uint8_t off[4] = {0, 0, 0, 0};
                for (uint16_t px = 0; px < n; px++) {
                    bool lit = (px <= head) && (px + band >= head);
                    led_output_write_raw(&strips[i], px, lit ? on : off);
                }
                if (i2sOwned[i]) any_i2s = true;
                continue;
            }
            hlStart[i] = 0;
        }

        // Universe pointer cache: getUniverseData() is a linear scan of the
        // universe pool (up to 32 entries). Caching the most recent hit cuts
        // it from O(pool × pixels) to ~O(pool × universes_per_strip).
        const uint8_t* cached_ubuf = nullptr;
        uint16_t       cached_univ = 0xFFFF;

        for (uint16_t px = 0; px < cfg.pixel_count; px++) {
            uint32_t slot = px / cfg.grouping;
            uint8_t  logical[4] = {0, 0, 0, 0};
            bool     skip = false;

            for (uint8_t c = 0; c < ch_pp; c++) {
                uint32_t ch_flat   = (uint32_t)(cfg.dmx_start - 1) + slot * ch_pp + c;
                uint16_t ch_univ   = cfg.universe_start + (uint16_t)(ch_flat / 512);
                uint16_t ch_offset = (uint16_t)(ch_flat % 512);

                if (ch_univ != cached_univ) {
                    cached_ubuf = getUniverseData(ch_univ);
                    cached_univ = ch_univ;
                }
                if (!cached_ubuf) { skip = true; break; }
                // Pre-applied brightness LUT replaces (val * brightness / 255).
                logical[c] = s_bright_lut[i][cached_ubuf[ch_offset + 1]];
            }
            if (skip) continue;

            uint8_t wire[4];
            for (uint8_t c = 0; c < ch_pp; c++)
                wire[c] = logical[cfg.color_order[c] & 3];

            uint16_t out_idx = cfg.invert ? (cfg.pixel_count - 1 - px) : px;
            led_output_write_raw(&strips[i], out_idx, wire);
        }
        if (i2sOwned[i]) any_i2s = true;
    }

    xSemaphoreGive(dmxBufferMutex);

    // Flush phase — kick all backends, then wait on each.
    if (any_i2s && i2sInitDone) i2s_par_trigger_frame();
    for (int i = 0; i < ELYON_NUM_OUTPUTS; i++) {
        if (stripActive[i] && !i2sOwned[i]) led_output_flush_async(&strips[i]);
    }
    for (int i = 0; i < ELYON_NUM_OUTPUTS; i++) {
        if (clockedActive[i])
            clocked_output_flush(&clockedStrips[i], elyonConfig.outputs[i].protocol);
    }
    if (any_i2s && i2sInitDone) i2s_par_wait_done();
    for (int i = 0; i < ELYON_NUM_OUTPUTS; i++) {
        if (stripActive[i] && !i2sOwned[i]) led_output_wait_done(&strips[i]);
    }

    stats_render_frame_end();
}

void elyonHighlightOutput(int idx) {
    if (idx < 0 || idx >= HW_LED_OUTPUT_COUNT) return;
    if (!stripActive[idx]) return;
    uint32_t t = millis();
    hlStart[idx] = t ? t : 1;
}

void fixtureHighlight() {}

void startDMX() {
    handleDMXenable = true;
}

void stopDMX() {
    handleDMXenable = false;

    bool any_i2s = false;
    for (int i = 0; i < ELYON_NUM_OUTPUTS; i++) {
        if (stripActive[i]) {
            led_output_clear(&strips[i]);
            if (i2sOwned[i]) any_i2s = true;
        }
        if (clockedActive[i]) {
            clocked_output_clear(&clockedStrips[i]);
            clocked_output_flush(&clockedStrips[i], elyonConfig.outputs[i].protocol);
        }
        if (pwmActive[i]) {
            uint8_t off = elyonConfig.outputs[i].pwm_invert ? 255 : 0;
            pwm_output_set(&pwms[i], off, 0);
        }
        if (relayActive[i]) {
            gpio_set_level((gpio_num_t)HW_LED_OUTPUT_PINS[i],
                           elyonConfig.outputs[i].relay_invert ? 1 : 0);
        }
    }

    if (any_i2s && i2sInitDone) {
        i2s_par_trigger_frame();
        i2s_par_wait_done();
    }
    for (int i = 0; i < ELYON_NUM_OUTPUTS; i++) {
        if (stripActive[i] && !i2sOwned[i]) led_output_flush(&strips[i]);
    }
}

#else // ! RAVLIGHT_MODULE_I2S_LED
// ─── RMT default path (one RMT channel per output) ──────────────────────────
// Validated path; per-output buffer in DRAM bounds the per-channel cost. Tested
// in production on QuinLED Dig-Octa, Penta Plus/Deca, Gledopto Elite 2D/4D.

// 2-wire clocked outputs (APA102 / SK9822 / P9813) — RMT path only for now.
// Each output owns DATA + a partner output's pin as CLOCK; the partner is marked
// LED_CLOCK_FOLLOWER and its own driver is skipped. The I2S path leaves clocked
// outputs unrendered until a parallel SPI/clocked backend is wired in.
static clocked_output_t clockedStrips[HW_LED_OUTPUT_COUNT];
static bool             clockedActive[HW_LED_OUTPUT_COUNT];

void initFixture() {
    // Register all universes needed by active outputs
    for (int i = 0; i < ELYON_NUM_OUTPUTS; i++) {
        const led_output_cfg_t& cfg = elyonConfig.outputs[i];
        uint8_t n = led_universe_count(&cfg);
        for (uint8_t u = 0; u < n; u++)
            registerDmxUniverse(cfg.universe_start + u);
    }

    uint32_t totalPixels = 0;
    for (int i = 0; i < ELYON_NUM_OUTPUTS; i++) {
        stripActive[i]   = false;
        pwmActive[i]     = false;
        relayActive[i]   = false;
        clockedActive[i] = false;
        const led_output_cfg_t& cfg = elyonConfig.outputs[i];
        int pin = HW_LED_OUTPUT_PINS[i];

        // Output marked as CLOCK partner of another clocked output → its pin is
        // driven by that owner; leave it un-initialised here.
        if (cfg.protocol == LED_CLOCK_FOLLOWER) {
            ESP_LOGI(TAG, "ch%d gpio%d CLOCK_FOLLOWER (consumed by clocked output)", i, pin);
            continue;
        }

        // 2-wire clocked chipset — uses partner output's pin as CLOCK.
        if (led_is_clocked(cfg.protocol)) {
            if (cfg.pixel_count == 0) continue;
            uint8_t partner = cfg.clock_partner_idx;
            if (partner >= HW_LED_OUTPUT_COUNT || partner == i) {
                ESP_LOGE(TAG, "ch%d clocked: invalid clock_partner_idx=%u (must be 0..%u and != self)",
                         i, partner, HW_LED_OUTPUT_COUNT - 1);
                continue;
            }
            int clock_pin = HW_LED_OUTPUT_PINS[partner];
            uint8_t ch_pp = led_ch_per_pixel(cfg.protocol);
            esp_err_t err = clocked_output_init(&clockedStrips[i], pin, clock_pin,
                                                cfg.pixel_count, ch_pp);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "ch%d clocked init failed err=%d", i, err);
                continue;
            }
            clockedActive[i] = true;
            char order_str[5];
            color_order_to_str(cfg.color_order, ch_pp, order_str);
            ESP_LOGI(TAG, "ch%d gpio%d (CLK gpio%d, ch%u) n=%d proto=%d order=%s univ=%d ch=%d group=%d inv=%d bri=%d",
                     i, pin, clock_pin, partner, cfg.pixel_count, cfg.protocol, order_str,
                     cfg.universe_start, cfg.dmx_start,
                     cfg.grouping, cfg.invert, cfg.brightness);
            continue;
        }

        if (cfg.protocol == LED_RELAY) {
            gpio_config_t gc = {};
            gc.pin_bit_mask  = 1ULL << pin;
            gc.mode          = GPIO_MODE_OUTPUT;
            gc.pull_up_en    = GPIO_PULLUP_DISABLE;
            gc.pull_down_en  = GPIO_PULLDOWN_DISABLE;
            gc.intr_type     = GPIO_INTR_DISABLE;
            gpio_config(&gc);
            gpio_set_level((gpio_num_t)pin, cfg.relay_invert ? 1 : 0);  // start in OFF state (respects active-low)
            relayActive[i] = true;
            ESP_LOGI(TAG, "ch%d gpio%d RELAY threshold=%u inv=%u univ=%d ch=%d",
                     i, pin, cfg.relay_threshold, cfg.relay_invert, cfg.universe_start, cfg.dmx_start);
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

    rebuildBrightnessLuts();
    elyon_render_task_start();
}

// Multi-universe renderer using flat channel index math.
// For each pixel, the flat channel address spans universe boundaries:
//   flat     = (dmx_start - 1) + slot * ch_per_pixel   (0-based)
//   universe = universe_start + flat / 512
//   ch_idx   = flat % 512                               (0-based within universe)
//   pool buf is 1-indexed: buf[ch_idx + 1] = channel ch_idx+1
// Runs on the dedicated render task (Core 0) — see elyon_render_task_fn above.
static void elyon_render_impl() {
    stats_render_frame_start();
    // No mutex around the render: the dedicated ArtNet/sACN receive task writes
    // the universe pool concurrently. Byte reads are atomic, so the worst case is
    // one strip showing a single frame of mixed old/new data — invisible on LEDs.
    for (int i = 0; i < ELYON_NUM_OUTPUTS; i++) {
        const led_output_cfg_t& cfg = elyonConfig.outputs[i];

        if (relayActive[i]) {
            const uint8_t* ubuf = getUniverseData(cfg.universe_start);
            if (ubuf) {
                uint8_t val = ubuf[cfg.dmx_start];  // 1-indexed buffer
                bool on = val >= cfg.relay_threshold;
                if (cfg.relay_invert) on = !on;
                gpio_set_level((gpio_num_t)HW_LED_OUTPUT_PINS[i], on ? 1 : 0);
            }
            continue;
        }

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

        // ── Clocked (APA102 / SK9822 / P9813) branch ──────────────────────────
        // Same universe-cache pixel decode as the WS branch, written to the
        // clocked_output_t buffer. Flush happens after the loop (sequential).
        if (clockedActive[i]) {
            uint8_t ch_pp = led_ch_per_pixel(cfg.protocol);
            const uint8_t* cached_ubuf = nullptr;
            uint16_t       cached_univ = 0xFFFF;

            for (uint16_t px = 0; px < cfg.pixel_count; px++) {
                uint32_t slot = px / cfg.grouping;
                uint8_t  logical[4] = {0, 0, 0, 0};
                bool     skip = false;

                for (uint8_t c = 0; c < ch_pp; c++) {
                    uint32_t ch_flat   = (uint32_t)(cfg.dmx_start - 1) + slot * ch_pp + c;
                    uint16_t ch_univ   = cfg.universe_start + (uint16_t)(ch_flat / 512);
                    uint16_t ch_offset = (uint16_t)(ch_flat % 512);

                    if (ch_univ != cached_univ) {
                        cached_ubuf = getUniverseData(ch_univ);
                        cached_univ = ch_univ;
                    }
                    if (!cached_ubuf) { skip = true; break; }
                    logical[c] = s_bright_lut[i][cached_ubuf[ch_offset + 1]];
                }
                if (skip) continue;

                uint8_t wire[4];
                for (uint8_t c = 0; c < ch_pp; c++)
                    wire[c] = logical[cfg.color_order[c] & 3];

                uint16_t out_idx = cfg.invert ? (cfg.pixel_count - 1 - px) : px;
                clocked_output_write_raw(&clockedStrips[i], out_idx, wire);
            }
            // Flush deferred to the sequential-flush phase below.
            continue;
        }

        if (!stripActive[i]) continue;

        // ── Highlight wipe (identification): a white band sweeps the strip ───
        if (hlStart[i]) {
            uint32_t el = millis() - hlStart[i];
            if (el < ELYON_HL_MS) {
                uint16_t n = cfg.pixel_count;
                uint16_t head = (uint16_t)((uint32_t)el * n / ELYON_HL_MS);
                uint16_t band = n / 12; if (band < 1) band = 1;
                uint8_t on[4]  = {255, 255, 255, 255};
                uint8_t off[4] = {0, 0, 0, 0};
                for (uint16_t px = 0; px < n; px++) {
                    bool lit = (px <= head) && (px + band >= head);
                    led_output_write_raw(&strips[i], px, lit ? on : off);
                }
                continue;
            }
            hlStart[i] = 0;
        }

        uint8_t ch_pp = led_ch_per_pixel(cfg.protocol);

        const uint8_t* cached_ubuf = nullptr;
        uint16_t       cached_univ = 0xFFFF;

        for (uint16_t px = 0; px < cfg.pixel_count; px++) {
            uint32_t slot = px / cfg.grouping;
            uint8_t  logical[4] = {0, 0, 0, 0};
            bool     skip = false;

            for (uint8_t c = 0; c < ch_pp; c++) {
                uint32_t ch_flat   = (uint32_t)(cfg.dmx_start - 1) + slot * ch_pp + c;
                uint16_t ch_univ   = cfg.universe_start + (uint16_t)(ch_flat / 512);
                uint16_t ch_offset = (uint16_t)(ch_flat % 512);

                if (ch_univ != cached_univ) {
                    cached_ubuf = getUniverseData(ch_univ);
                    cached_univ = ch_univ;
                }
                if (!cached_ubuf) { skip = true; break; }
                logical[c] = (uint16_t)cached_ubuf[ch_offset + 1] * cfg.brightness / 255;
            }

            if (skip) continue;

            uint8_t wire[4];
            for (uint8_t c = 0; c < ch_pp; c++)
                wire[c] = logical[cfg.color_order[c] & 3];

            uint16_t out_idx = cfg.invert ? (cfg.pixel_count - 1 - px) : px;
            led_output_write_raw(&strips[i], out_idx, wire);
        }
    }

    // Submit all strips simultaneously (non-blocking).
    for (int i = 0; i < ELYON_NUM_OUTPUTS; i++) {
        if (stripActive[i]) led_output_flush_async(&strips[i]);
    }

    for (int i = 0; i < ELYON_NUM_OUTPUTS; i++) {
        if (stripActive[i]) led_output_wait_done(&strips[i]);
    }

    // Clocked outputs: bit-bang, sequential (no parallel hardware peripheral
    // shared between them — each holds its own DATA + CLOCK GPIO pair).
    for (int i = 0; i < ELYON_NUM_OUTPUTS; i++) {
        if (clockedActive[i]) {
            clocked_output_flush(&clockedStrips[i], elyonConfig.outputs[i].protocol);
        }
    }

    stats_render_frame_end();
}

// Start a white-wipe highlight on one pixel output (identification). No-op for
// PWM/relay outputs or out-of-range indices.
void elyonHighlightOutput(int idx) {
    if (idx < 0 || idx >= HW_LED_OUTPUT_COUNT) return;
    if (!stripActive[idx]) return;
    uint32_t t = millis();
    hlStart[idx] = t ? t : 1;
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
            led_output_flush_async(&strips[i]);
        }
        if (pwmActive[i]) {
            uint8_t off = elyonConfig.outputs[i].pwm_invert ? 255 : 0;
            pwm_output_set(&pwms[i], off, 0);
        }
        if (relayActive[i]) {
            // OFF state respects active-low inversion
            gpio_set_level((gpio_num_t)HW_LED_OUTPUT_PINS[i],
                           elyonConfig.outputs[i].relay_invert ? 1 : 0);
        }
        if (clockedActive[i]) {
            clocked_output_clear(&clockedStrips[i]);
            clocked_output_flush(&clockedStrips[i], elyonConfig.outputs[i].protocol);
        }
    }
    for (int i = 0; i < ELYON_NUM_OUTPUTS; i++) {
        if (stripActive[i]) led_output_wait_done(&strips[i]);
    }
}

#endif // RAVLIGHT_MODULE_I2S_LED

#endif // RAVLIGHT_FIXTURE_ELYON
