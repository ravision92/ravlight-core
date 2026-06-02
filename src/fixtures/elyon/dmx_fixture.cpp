#ifdef RAVLIGHT_FIXTURE_ELYON
#include "fixtures/elyon/dmx_fixture.h"
#include "fixtures/elyon/fixture.h"
#include "core/led_output.h"
#include "core/clocked_output.h"
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

// Per-output highlight wipe (identification). 0 = inactive, else millis() at start.
static uint32_t     hlStart[HW_LED_OUTPUT_COUNT] = {0};
#define ELYON_HL_MS 1500

// LEDC PWM outputs (LED_PWM protocol).
// channel_idx < 8 → LEDC_LOW_SPEED_MODE; channel_idx 8–15 → LEDC_HIGH_SPEED_MODE.
static pwm_output_t pwms[HW_LED_OUTPUT_COUNT];
static bool         pwmActive[HW_LED_OUTPUT_COUNT];

// GPIO relay outputs (LED_RELAY protocol).
static bool relayActive[HW_LED_OUTPUT_COUNT];

// 2-wire clocked outputs (APA102 / SK9822 / P9813).
// Each output owns DATA + a partner output's pin as CLOCK; the partner is marked
// LED_CLOCK_FOLLOWER and its own driver is skipped.
static clocked_output_t clockedStrips[HW_LED_OUTPUT_COUNT];
static bool             clockedActive[HW_LED_OUTPUT_COUNT];

// ── Public API ────────────────────────────────────────────────────────────────

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
}

// Multi-universe renderer using flat channel index math.
// For each pixel, the flat channel address spans universe boundaries:
//   flat     = (dmx_start - 1) + slot * ch_per_pixel   (0-based)
//   universe = universe_start + flat / 512
//   ch_idx   = flat % 512                               (0-based within universe)
//   pool buf is 1-indexed: buf[ch_idx + 1] = channel ch_idx+1
void handleDMX() {
    if (!handleDMXenable) return;

// No mutex around the render: the dedicated ArtNet/sACN receive task writes
    // the universe pool concurrently. Byte reads are atomic, so the worst case is
    // one strip showing a single frame of mixed old/new data — invisible on LEDs.
    // Holding the mutex here would block the receive task for the whole render
    // (~1.5 ms for 8×325 px) and overflow the lwIP UDP mailbox during Resolume's
    // 16-universe burst, dropping every universe past the first ~6 (= 3 outputs).
    for (int i = 0; i < ELYON_NUM_OUTPUTS; i++) {
        const led_output_cfg_t& cfg = elyonConfig.outputs[i];

        // ── Relay branch ─────────────────────────────────────────────────────────
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
                    logical[c] = (uint16_t)cached_ubuf[ch_offset + 1] * cfg.brightness / 255;
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

        // ── Highlight wipe (identification): a white band sweeps the strip ───────
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
                continue;   // skip DMX render for this output this frame
            }
            hlStart[i] = 0;  // wipe finished → resume DMX
        }

        uint8_t ch_pp = led_ch_per_pixel(cfg.protocol);

        // Universe cache: avoids repeated getUniverseData() calls for pixels in the same universe.
        // Reset per strip; hoisted outside the pixel loop so it persists across universe transitions.
        const uint8_t* cached_ubuf = nullptr;
        uint16_t       cached_univ = 0xFFFF;

        for (uint16_t px = 0; px < cfg.pixel_count; px++) {
            uint32_t slot = px / cfg.grouping;
            uint8_t  logical[4] = {0, 0, 0, 0};
            bool     skip = false;

            // Resolve each channel independently so split pixels (spanning a universe boundary)
            // read their bytes from the correct universe rather than breaking the loop.
            for (uint8_t c = 0; c < ch_pp; c++) {
                uint32_t ch_flat   = (uint32_t)(cfg.dmx_start - 1) + slot * ch_pp + c;
                uint16_t ch_univ   = cfg.universe_start + (uint16_t)(ch_flat / 512);
                uint16_t ch_offset = (uint16_t)(ch_flat % 512);  // 0-based within universe

                if (ch_univ != cached_univ) {
                    cached_ubuf = getUniverseData(ch_univ);
                    cached_univ = ch_univ;
                }
                if (!cached_ubuf) { skip = true; break; }
                logical[c] = (uint16_t)cached_ubuf[ch_offset + 1] * cfg.brightness / 255;
            }

            if (skip) continue;

            // Apply color order: wire[c] = logical[color_order[c]]
            uint8_t wire[4];
            for (uint8_t c = 0; c < ch_pp; c++)
                wire[c] = logical[cfg.color_order[c] & 3];

            uint16_t out_idx = cfg.invert ? (cfg.pixel_count - 1 - px) : px;
            led_output_write_raw(&strips[i], out_idx, wire);
        }
        // Flush deferred to the parallel-submit phase below — do NOT flush here.
    }

    // Submit all strips simultaneously (non-blocking); each channel starts
    // transmitting in parallel via the RMT translator ISR.
    for (int i = 0; i < ELYON_NUM_OUTPUTS; i++) {
        if (stripActive[i]) led_output_flush_async(&strips[i]);
    }

    // Wait for all transmissions to complete outside the mutex so new ArtNet
    // packets can be received and buffered while the current frame is on the wire.
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
}

void fixtureHighlight() {}

// Start a white-wipe highlight on one pixel output (identification). No-op for
// PWM/relay outputs or out-of-range indices.
void elyonHighlightOutput(int idx) {
    if (idx < 0 || idx >= HW_LED_OUTPUT_COUNT) return;
    if (!stripActive[idx]) return;
    uint32_t t = millis();
    hlStart[idx] = t ? t : 1;
}

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

#endif // RAVLIGHT_FIXTURE_ELYON
