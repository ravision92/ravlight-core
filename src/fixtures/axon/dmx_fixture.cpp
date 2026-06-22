#ifdef RAVLIGHT_FIXTURE_AXON
// Axon — network to RS-485 DMX bridge + optional Elyon-style LED outputs.
//
// The bridge itself is a core feature (dmx_manager pushes the universe at
// dmxConfig.startUniverse out the RS-485 port when dmxOutputEnabled is on
// and dmxInput is ArtNet/sACN). This file just registers universes and,
// when AXON_HAS_LED is defined, runs an Elyon-lite render loop over the
// configured 2 outputs.

#include "fixture_config.h"
#include "fixtures/axon/fixture.h"
#include "dmx_manager.h"
#include "config.h"
#include "esp_log.h"
#include <math.h>

#ifdef AXON_HAS_LED
#include "core/led_output.h"
#include "core/output_config.h"
#include "driver/rmt.h"
#endif

static const char* TAG = "AXON";

bool handleDMXenable = true;

#ifdef AXON_HAS_LED
// ── Per-output backend state ───────────────────────────────────────────────
static led_output_t strips[AXON_NUM_LED_OUTPUTS];
static bool         stripActive[AXON_NUM_LED_OUTPUTS] = {false};
static uint32_t     hlStart[AXON_NUM_LED_OUTPUTS]      = {0};
#define AXON_HL_MS  2000  // identification wipe duration

// Pre-applied brightness × gamma LUT — one 256-byte table per output. Same
// trick Elyon uses to keep the per-pixel render to a single table lookup.
static uint8_t s_bright_lut[AXON_NUM_LED_OUTPUTS][256];

static void rebuildBrightnessLuts() {
    for (int i = 0; i < AXON_NUM_LED_OUTPUTS; i++) {
        const led_output_cfg_t& cfg = axonConfig.ledOutputs[i];
        float gamma = (float)cfg.gamma_x10 / 10.0f;
        for (int v = 0; v < 256; v++) {
            float norm = (float)v / 255.0f;
            float curve = (gamma <= 1.0f) ? norm : powf(norm, gamma);
            uint32_t scaled = (uint32_t)(curve * cfg.brightness + 0.5f);
            if (scaled > 255) scaled = 255;
            s_bright_lut[i][v] = (uint8_t)scaled;
        }
    }
}

static inline bool proto_is_ws(led_protocol_t p) {
    return p == LED_WS2811 || p == LED_WS2812B ||
           p == LED_SK6812  || p == LED_WS2814  ||
           p == LED_WS2815  || p == LED_TM1814  ||
           p == LED_TM1914;
}
#endif // AXON_HAS_LED

void initFixture() {
    // Register the bridge universe so ArtNet/sACN packets for it land in the
    // pool — the core's wired-DMX TX path then pushes its 512 bytes out the
    // RS-485 driver each frame.
    registerDmxUniverse(dmxConfig.startUniverse);

#ifdef AXON_HAS_LED
    // Register every universe touched by an enabled LED output, then init
    // the per-output driver.
    for (int i = 0; i < AXON_NUM_LED_OUTPUTS; i++) {
        const led_output_cfg_t& cfg = axonConfig.ledOutputs[i];
        uint8_t n = led_universe_count(&cfg);
        for (uint8_t u = 0; u < n; u++)
            registerDmxUniverse(cfg.universe_start + u);
    }

    // 2 outputs → RMT_CHANNEL_0 and RMT_CHANNEL_1, mem_blocks=1 (Elyon's
    // tight-budget config; we only have two strips, but staying at 1 keeps
    // the I2S backend interchangeable when it gets wired in).
    for (int i = 0; i < AXON_NUM_LED_OUTPUTS; i++) {
        const led_output_cfg_t& cfg = axonConfig.ledOutputs[i];
        if (!proto_is_ws(cfg.protocol) || cfg.pixel_count == 0) {
            stripActive[i] = false;
            continue;
        }
        if (led_output_init(&strips[i], HW_LED_OUTPUT_PINS[i], cfg.pixel_count,
                            (rmt_channel_t)(RMT_CHANNEL_0 + i),
                            /*mem_blocks=*/1,
                            /*channels=*/led_ch_per_pixel(cfg.protocol)) == ESP_OK) {
            stripActive[i] = true;
        }
    }
    rebuildBrightnessLuts();
    ESP_LOGI(TAG, "Axon ready: bridge u%d → RS-485, %d LED outputs",
             dmxConfig.startUniverse, AXON_NUM_LED_OUTPUTS);
#else
    ESP_LOGI(TAG, "Axon ready: bridge u%d → RS-485 (no LED outputs)",
             dmxConfig.startUniverse);
#endif
}

void handleDMX() {
    // Apply any deferred ArtSync swap so the universe pool's active buffer
    // sees the latest frame. Without this, controllers in Art-Net 4 sync
    // mode (Resolume default) would silently render stale data on the LED
    // outputs and /dmxdata would always return zeros, even though packets
    // are arriving. Cheap: only does work when s_swap_pending is true.
    dmxApplyPendingSwap();

#ifdef AXON_HAS_LED
    if (!handleDMXenable) return;

    for (int i = 0; i < AXON_NUM_LED_OUTPUTS; i++) {
        if (!stripActive[i]) continue;
        const led_output_cfg_t& cfg = axonConfig.ledOutputs[i];
        uint8_t ch_pp = led_ch_per_pixel(cfg.protocol);

        // Highlight wipe (identification)
        if (hlStart[i]) {
            uint32_t el = millis() - hlStart[i];
            if (el < AXON_HL_MS) {
                uint16_t n = cfg.pixel_count;
                uint16_t head = (uint16_t)((uint32_t)el * n / AXON_HL_MS);
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

        // Normal render — read configured universe block, apply
        // brightness/gamma LUT, swap to wire colour order.
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
            led_output_write_raw(&strips[i], out_idx, wire);
        }
    }

    // Async flush + wait — RMT/I2S driven by led_output internals.
    for (int i = 0; i < AXON_NUM_LED_OUTPUTS; i++)
        if (stripActive[i]) led_output_flush_async(&strips[i]);
    for (int i = 0; i < AXON_NUM_LED_OUTPUTS; i++)
        if (stripActive[i]) led_output_wait_done(&strips[i]);
#endif
}

void startDMX() { handleDMXenable = true; }
void stopDMX()  { handleDMXenable = false; }

void fixtureHighlight() {
#ifdef AXON_HAS_LED
    // Trigger the white wipe on every active LED output simultaneously.
    for (int i = 0; i < AXON_NUM_LED_OUTPUTS; i++)
        if (stripActive[i]) hlStart[i] = millis();
#endif
}

#ifdef AXON_HAS_LED
void axonHighlightLed(int idx) {
    if (idx < 0 || idx >= AXON_NUM_LED_OUTPUTS) return;
    if (stripActive[idx]) hlStart[idx] = millis();
}
#endif

#endif // RAVLIGHT_FIXTURE_AXON
