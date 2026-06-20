#if defined(RAVLIGHT_FIXTURE_ELYON) && defined(RAVLIGHT_MODULE_I2S_LED)
// I2S parallel WS281x driver — thin wrapper over hpwit's I2SClocklessLedDriver
// (https://github.com/hpwit/I2SClocklessLedDriver). The hpwit lib is battle-tested
// in WLED/ESPHome and handles the I2S0 LCD parallel register dance correctly on
// ESP32-D0WD silicon — the previous in-house port stalled the DMA on this chip.
//
// We expose the existing i2s_par_* API so dmx_fixture.cpp stays unchanged.
// Wire width (3 or 4 bytes per pixel) is selected at init time from cfg.bytes_pp.
// All channels share the same wire width — chained pixels on a strip would
// misalign if the strip's native width differs, so the caller is expected to
// only group same-width outputs on the I2S bus.

#include <Arduino.h>
#include "core/i2s_parallel_output.h"
#include "I2SClocklessLedDriver.h"
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <string.h>

static const char* LTAG = "I2SPAR";

// Driver strip index for each of our 8 channel slots. -1 = slot unused.
static int8_t  s_drv_strip_idx[I2S_PAR_MAX_CH];

// Source buffer registered per channel via i2s_par_set_source.
struct Src {
    const uint8_t* buf;
    uint16_t       n_pixels;
    uint8_t        bytes_pp;
};
static Src       s_src[I2S_PAR_MAX_CH];
static uint8_t   s_n_ch       = 0;
static uint16_t  s_max_per_ch = 0;
static uint8_t   s_wire_bpp   = 3;     // 3 (RGB) or 4 (RGBW); device-wide
static bool      s_inited     = false;

static I2SClocklessLedDriver s_driver;
static uint8_t*  s_leds_buf   = nullptr;  // num_strips × max_per_ch × s_wire_bpp

esp_err_t i2s_par_init(const i2s_par_cfg_t* cfg) {
    if (!cfg || cfg->n_channels == 0 || cfg->max_pixels_per_ch == 0)
        return ESP_ERR_INVALID_ARG;

    s_n_ch       = cfg->n_channels < I2S_PAR_MAX_CH ? cfg->n_channels : I2S_PAR_MAX_CH;
    s_max_per_ch = cfg->max_pixels_per_ch;
    s_wire_bpp   = (cfg->bytes_pp == 4) ? 4 : 3;  // anything else → RGB
    memset(s_src, 0, sizeof(s_src));
    for (int ch = 0; ch < I2S_PAR_MAX_CH; ch++) s_drv_strip_idx[ch] = -1;

    // Collect active pins (gpio_pins[i] >= 0). Order matters: the driver gets a
    // contiguous array and assigns strip 0 to pins[0] etc., so we remember the
    // mapping channel→strip for the render loop.
    uint8_t pins[I2S_PAR_MAX_CH];
    uint8_t n_strips = 0;
    for (int ch = 0; ch < s_n_ch; ch++) {
        if (cfg->gpio_pins[ch] >= 0) {
            pins[n_strips] = (uint8_t)cfg->gpio_pins[ch];
            s_drv_strip_idx[ch] = (int8_t)n_strips;
            n_strips++;
        }
    }

    if (n_strips == 0) {
        ESP_LOGW(LTAG, "no active strips → skipping I2S init");
        return ESP_OK;
    }
    if (s_inited) {
        // hpwit driver supports updateDriver() but our existing flow only inits once
        // per boot — bail out rather than re-init mid-frame.
        ESP_LOGW(LTAG, "already inited — skipping re-init");
        return ESP_OK;
    }

    // Allocate the LED byte buffer ourselves — hpwit's initled stores whatever
    // pointer we pass without alloc'ing it.
    size_t leds_bytes = (size_t)n_strips * s_max_per_ch * s_wire_bpp;
    s_leds_buf = (uint8_t*)heap_caps_calloc(1, leds_bytes, MALLOC_CAP_8BIT);
    if (!s_leds_buf) {
        ESP_LOGE(LTAG, "leds buffer alloc failed (%u B)", (unsigned)leds_bytes);
        return ESP_ERR_NO_MEM;
    }

    // Color arrangement: ORDER_RGB / ORDER_RGBW stores caller's bytes verbatim as
    // wire bytes 0..N-1. Our render loop already applied per-output color_order,
    // so wire[i] is the byte the strip expects in slot i.
    ColorArrangement order = (s_wire_bpp == 4) ? ORDER_RGBW : ORDER_RGB;
    s_driver.initled(s_leds_buf, pins, n_strips, s_max_per_ch, order);
    s_inited = true;

    ESP_LOGI(LTAG, "init: %u strips, %u px/strip, %s, leds_buf=%u B",
             (unsigned)n_strips, (unsigned)s_max_per_ch,
             s_wire_bpp == 4 ? "RGBW" : "RGB", (unsigned)leds_bytes);
    return ESP_OK;
}

void i2s_par_set_source(uint8_t ch, const uint8_t* buf, uint16_t n_pixels,
                        uint8_t bytes_pp) {
    if (ch >= s_n_ch) return;
    s_src[ch].buf      = buf;
    s_src[ch].n_pixels = n_pixels;
    s_src[ch].bytes_pp = bytes_pp;
}

void i2s_par_trigger_frame(void) {
    if (!s_inited) return;

    // Copy each registered wire-order buffer into the driver's internal LED array
    // and kick off DMA. When source and device wire widths match (the common case
    // on a homogenous bus) we bulk-memcpy the whole strip at once — 4-8× faster
    // than the equivalent setPixel() loop on ESP32 word-aligned DMA buffers, and
    // the heaviest CPU contribution to the I2S render path. On mixed-width buses
    // (RGB source on RGBW device) we fall back to per-pixel fix-up.
    const uint32_t strip_stride = (uint32_t)s_max_per_ch * s_wire_bpp;
    for (int ch = 0; ch < s_n_ch; ch++) {
        int8_t strip_idx = s_drv_strip_idx[ch];
        if (strip_idx < 0) continue;
        const Src& s = s_src[ch];
        if (!s.buf || s.n_pixels == 0) continue;

        uint8_t* dst = s_leds_buf + (uint32_t)strip_idx * strip_stride;

        if (s.bytes_pp == s_wire_bpp) {
            memcpy(dst, s.buf, (size_t)s.n_pixels * s_wire_bpp);
        } else {
            // Mixed RGB↔RGBW: zero-fill W (RGB on RGBW bus) or drop W (RGBW on
            // RGB bus). Per-pixel slow path; only hit if outputs share a bus
            // but differ in native channel count.
            for (uint16_t px = 0; px < s.n_pixels; px++) {
                const uint8_t* p = s.buf + (uint32_t)px * s.bytes_pp;
                uint8_t*       d = dst   + (uint32_t)px * s_wire_bpp;
                d[0] = p[0]; d[1] = p[1]; d[2] = p[2];
                if (s_wire_bpp == 4) d[3] = (s.bytes_pp == 4) ? p[3] : 0;
            }
        }
    }

    // WAIT mode: showPixels blocks until DMA + reset pulse complete (~1.5 ms for
    // 30 px @ 800 kHz). The NO_WAIT branch of the lib has a 500 ms semaphore wait
    // that caps refresh at ~2 fps on this build — WAIT runs at full strip speed.
    s_driver.showPixels(WAIT);
}

void i2s_par_wait_done(void) {
    // showPixels(WAIT) already blocked until completion — nothing more to wait on.
}

#endif // RAVLIGHT_FIXTURE_ELYON && RAVLIGHT_MODULE_I2S_LED
