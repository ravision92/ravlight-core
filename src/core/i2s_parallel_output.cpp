#if defined(RAVLIGHT_FIXTURE_ELYON) && defined(RAVLIGHT_MODULE_I2S_LED)
// I2S parallel WS281x driver — thin wrapper over hpwit's I2SClocklessLedDriver
// (https://github.com/hpwit/I2SClocklessLedDriver). The hpwit lib is battle-tested
// in WLED/ESPHome and handles the I2S0 LCD parallel register dance correctly on
// ESP32-D0WD silicon — the previous in-house port stalled the DMA on this chip.
//
// We expose the existing i2s_par_* API so dmx_fixture.cpp stays unchanged.
// All outputs are driven as RGB (24 bits per pixel) for now — RGBW strips get
// their W channel forced to 0 until per-output protocol-aware mode is added.

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
static bool      s_inited     = false;

static I2SClocklessLedDriver s_driver;
static uint8_t*  s_leds_buf   = nullptr;  // num_strips × max_per_ch × 3 bytes (RGB)

esp_err_t i2s_par_init(const i2s_par_cfg_t* cfg) {
    if (!cfg || cfg->n_channels == 0 || cfg->max_pixels_per_ch == 0)
        return ESP_ERR_INVALID_ARG;

    s_n_ch       = cfg->n_channels < I2S_PAR_MAX_CH ? cfg->n_channels : I2S_PAR_MAX_CH;
    s_max_per_ch = cfg->max_pixels_per_ch;
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
    // pointer we pass without alloc'ing. RGB mode → 3 bytes per pixel.
    size_t leds_bytes = (size_t)n_strips * s_max_per_ch * 3;
    s_leds_buf = (uint8_t*)heap_caps_calloc(1, leds_bytes, MALLOC_CAP_8BIT);
    if (!s_leds_buf) {
        ESP_LOGE(LTAG, "leds buffer alloc failed (%u B)", (unsigned)leds_bytes);
        return ESP_ERR_NO_MEM;
    }

    // ORDER_RGB: the bytes passed to setPixel(r,g,b) are stored verbatim as wire
    // bytes 0/1/2. Our render loop has already applied per-output color_order,
    // so wire[0..2] is exactly the byte order the strip expects.
    s_driver.initled(s_leds_buf, pins, n_strips, s_max_per_ch, ORDER_RGB);
    s_inited = true;

    ESP_LOGI(LTAG, "init: %u strips, %u px/strip, leds_buf=%u B",
             (unsigned)n_strips, (unsigned)s_max_per_ch, (unsigned)leds_bytes);
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
    // and kick off DMA. NO_WAIT returns immediately; i2s_par_wait_done() is a no-op
    // because the next showPixels() automatically waits for the previous one.
    for (int ch = 0; ch < s_n_ch; ch++) {
        int8_t strip_idx = s_drv_strip_idx[ch];
        if (strip_idx < 0) continue;
        const Src& s = s_src[ch];
        if (!s.buf || s.n_pixels == 0) continue;

        for (uint16_t px = 0; px < s.n_pixels; px++) {
            const uint8_t* p = s.buf + (uint32_t)px * s.bytes_pp;
            uint32_t pos = (uint32_t)strip_idx * s_max_per_ch + px;
            // RGB-only path: ignore the W byte if present. Driver is configured
            // with ORDER_RGB so setPixel(pos, b0, b1, b2) emits b0, b1, b2 on the wire.
            s_driver.setPixel(pos, p[0], p[1], p[2]);
        }
    }

    s_driver.showPixels(NO_WAIT);
}

void i2s_par_wait_done(void) {
    // hpwit driver auto-waits on the next showPixels() call.
    // Nothing to do here.
}

#endif // RAVLIGHT_FIXTURE_ELYON && RAVLIGHT_MODULE_I2S_LED
