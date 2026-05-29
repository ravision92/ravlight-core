#include "core/led_output.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

static const char* TAG = "LED";

// WS2812B-compatible 800 kHz protocol (clk_div=4, APB=80 MHz → 50 ns/tick)
// FastLED's WS2811 type on ESP32 also uses these 800 kHz timings.
// The original WS2811 400 kHz spec (T0H=500 ns) sits above this strip's
// ~600 ns threshold and is misread as '1', causing low-brightness white glitch.
#define WS_T0H  8   //  400 ns  — safely below the ~600 ns '0'/'1' threshold
#define WS_T0L 17   //  850 ns
#define WS_T1H 16   //  800 ns
#define WS_T1L  9   //  450 ns
// Period: T0H+T0L = T1H+T1L = 25 ticks = 1250 ns (800 kHz) ✓

// RMT translator callback — converts raw RGB(W) bytes to RMT items in ISR context.
// Placed in IRAM to avoid cache misses during ISR refill of the hardware FIFO.
// Called multiple times per strip per frame (once per mem_block worth of items).
static void IRAM_ATTR ws2812_to_rmt(const void* src, rmt_item32_t* dest, size_t src_size,
                                     size_t wanted_num, size_t* translated_size, size_t* item_num) {
    const uint8_t* p = (const uint8_t*)src;
    size_t size = 0, num = 0;
    while (size < src_size && num + 8 <= wanted_num) {
        uint8_t byte = *p++;
        for (int bit = 7; bit >= 0; bit--) {
            if (byte & (1 << bit)) {
                dest->level0 = 1; dest->duration0 = WS_T1H;
                dest->level1 = 0; dest->duration1 = WS_T1L;
            } else {
                dest->level0 = 1; dest->duration0 = WS_T0H;
                dest->level1 = 0; dest->duration1 = WS_T0L;
            }
            dest++;
            num++;
        }
        size++;
    }
    *translated_size = size;
    *item_num = num;
}

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

    rmt_config_t cfg = RMT_DEFAULT_CONFIG_TX((gpio_num_t)gpio_num, channel);
    cfg.clk_div       = 4;          // 80 MHz / 4 = 20 MHz → 50 ns/tick
    cfg.mem_block_num = mem_blocks;

    esp_err_t err = rmt_config(&cfg);
    if (err != ESP_OK) { ESP_LOGE(TAG, "rmt_config err %d", err); return err; }

    err = rmt_driver_install(channel, 0, 0);
    if (err != ESP_OK) { ESP_LOGE(TAG, "rmt_driver_install err %d", err); return err; }

    // Translator callback enables rmt_write_sample() for non-blocking parallel output.
    // Also harmless for channels using the blocking led_output_flush() path (Veyron).
    rmt_translator_init(channel, ws2812_to_rmt);

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
    // Blocking: submit via translator callback and wait for TX done.
    // 100 ms timeout guards against GPIO/UART conflicts (e.g. GPIO 1 = UART0 TX).
    rmt_write_sample(out->channel, out->buf, (size_t)out->n_pixels * out->channels, false);
    esp_err_t werr = rmt_wait_tx_done(out->channel, pdMS_TO_TICKS(100));
    if (werr != ESP_OK) {
        ESP_LOGW(TAG, "ch%d tx timeout (GPIO/UART conflict?)", out->channel);
    }
}

// Non-blocking: submit via translator callback, return immediately.
// All channels can transmit in parallel — caller must call led_output_wait_done()
// before modifying buf or calling flush_async again on the same channel.
void led_output_flush_async(led_output_t* out) {
    rmt_write_sample(out->channel, out->buf, (size_t)out->n_pixels * out->channels, false);
}

static volatile uint32_t s_tx_timeouts = 0;

void led_output_wait_done(led_output_t* out) {
    esp_err_t werr = rmt_wait_tx_done(out->channel, pdMS_TO_TICKS(100));
    if (werr != ESP_OK) {
        s_tx_timeouts++;
        ESP_LOGW(TAG, "ch%d tx timeout", out->channel);
    }
}

uint32_t led_output_timeout_count(void) { return s_tx_timeouts; }

void led_output_clear(led_output_t* out) {
    memset(out->buf, 0, out->n_pixels * out->channels);
}

void led_output_deinit(led_output_t* out) {
    rmt_driver_uninstall(out->channel);
    free(out->buf);
    out->buf = NULL;
    // g_rmt_items is shared — not freed here; it persists for the next init cycle.
}
