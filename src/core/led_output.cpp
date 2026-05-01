#include "core/led_output.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

static const char* TAG = "LED";

// Shared pre-encoded RMT item buffer — grows to fit the largest active output.
// All led_output_flush() calls happen sequentially inside handleDMX(), so a single
// shared buffer is safe and reduces heap from N×item_buf down to one allocation
// sized for the largest strip (300px = 7201 items × 4 B = 28.8 KB instead of N×28.8 KB).
static rmt_item32_t* g_rmt_items       = NULL;
static size_t        g_rmt_items_count = 0;

static esp_err_t ensure_shared_buf(size_t needed) {
    if (needed <= g_rmt_items_count) return ESP_OK;
    rmt_item32_t* p = (rmt_item32_t*)realloc(g_rmt_items, needed * sizeof(rmt_item32_t));
    if (!p) return ESP_ERR_NO_MEM;
    g_rmt_items       = p;
    g_rmt_items_count = needed;
    return ESP_OK;
}

// WS2812B-compatible 800 kHz protocol (clk_div=4, APB=80 MHz → 50 ns/tick)
// FastLED's WS2811 type on ESP32 also uses these 800 kHz timings.
// The original WS2811 400 kHz spec (T0H=500 ns) sits above this strip's
// ~600 ns threshold and is misread as '1', causing low-brightness white glitch.
#define WS_T0H  8   //  400 ns  — safely below the ~600 ns '0'/'1' threshold
#define WS_T0L 17   //  850 ns
#define WS_T1H 16   //  800 ns
#define WS_T1L  9   //  450 ns
// Period: T0H+T0L = T1H+T1L = 25 ticks = 1250 ns (800 kHz) ✓

esp_err_t led_output_init(led_output_t* out, int gpio_num, uint16_t n_pixels,
                           rmt_channel_t channel, uint8_t mem_blocks, uint8_t channels) {
    out->channel  = channel;
    out->n_pixels = n_pixels;
    out->channels = channels;

    out->buf = (uint8_t*)calloc(n_pixels * channels, 1);
    if (!out->buf) {
        ESP_LOGE(TAG, "buf alloc failed ch%d", channel);
        return ESP_ERR_NO_MEM;
    }

    // Grow the shared RMT item buffer if this output is larger than any seen so far.
    size_t item_count = (size_t)n_pixels * channels * 8 + 1;
    if (ensure_shared_buf(item_count) != ESP_OK) {
        ESP_LOGE(TAG, "shared items alloc failed ch%d", channel);
        free(out->buf);
        out->buf = NULL;
        return ESP_ERR_NO_MEM;
    }

    rmt_config_t cfg = RMT_DEFAULT_CONFIG_TX((gpio_num_t)gpio_num, channel);
    cfg.clk_div       = 4;          // 80 MHz / 4 = 20 MHz → 50 ns/tick
    cfg.mem_block_num = mem_blocks;

    esp_err_t err = rmt_config(&cfg);
    if (err != ESP_OK) { ESP_LOGE(TAG, "rmt_config err %d", err); return err; }

    err = rmt_driver_install(channel, 0, 0);
    if (err != ESP_OK) { ESP_LOGE(TAG, "rmt_driver_install err %d", err); return err; }

    ESP_LOGI(TAG, "ch%d gpio%d n=%d mem=%d", channel, gpio_num, n_pixels, mem_blocks);
    return ESP_OK;
}

void led_output_set_pixel(led_output_t* out, uint16_t idx, uint8_t r, uint8_t g, uint8_t b) {
    if (idx >= out->n_pixels) return;
    out->buf[idx * out->channels]     = r;
    out->buf[idx * out->channels + 1] = g;
    out->buf[idx * out->channels + 2] = b;
}

void led_output_write_raw(led_output_t* out, uint16_t idx, const uint8_t* src) {
    if (idx >= out->n_pixels) return;
    memcpy(out->buf + idx * out->channels, src, out->channels);
}

void led_output_flush(led_output_t* out) {
    // Encode the entire RGB buffer to RMT items before calling the driver.
    // This makes the ISR refill path a simple DRAM read — no translator callback,
    // no computation in interrupt context — immune to WiFi/Ethernet ISR preemption.
    rmt_item32_t* item = g_rmt_items;
    const uint8_t* p   = out->buf;
    const int nbytes   = out->n_pixels * out->channels;

    for (int i = 0; i < nbytes; i++) {
        uint8_t byte = p[i];
        for (int bit = 7; bit >= 0; bit--) {
            if (byte & (1 << bit)) {
                item->level0 = 1; item->duration0 = WS_T1H;
                item->level1 = 0; item->duration1 = WS_T1L;
            } else {
                item->level0 = 1; item->duration0 = WS_T0H;
                item->level1 = 0; item->duration1 = WS_T0L;
            }
            item++;
        }
    }
    // EOT marker — already zeroed at alloc, but set explicitly to be safe.
    item->val = 0;

    // Non-blocking submit; then wait with a finite timeout.
    // Passing wait_tx_done=true to rmt_write_items blocks on an internal semaphore
    // that relies on the RMT TX_END interrupt. On pins shared with UART (e.g. GPIO 1
    // on boards that reuse UART0_TX as a LED output), the GPIO matrix contention can
    // prevent the interrupt from firing, causing the semaphore to never be given and
    // the TWDT to fire. A 100 ms timeout prevents a crash loop while still providing
    // practical back-pressure between back-to-back flush calls.
    rmt_write_items(out->channel, g_rmt_items, nbytes * 8, false);
    esp_err_t werr = rmt_wait_tx_done(out->channel, pdMS_TO_TICKS(100));
    if (werr != ESP_OK) {
        ESP_LOGW(TAG, "ch%d tx timeout (GPIO/UART conflict?)", out->channel);
    }
}

void led_output_clear(led_output_t* out) {
    memset(out->buf, 0, out->n_pixels * out->channels);
}

void led_output_deinit(led_output_t* out) {
    rmt_driver_uninstall(out->channel);
    free(out->buf);
    out->buf = NULL;
    // g_rmt_items is shared — not freed here; it persists for the next init cycle.
}
