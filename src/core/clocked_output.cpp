#include "core/clocked_output.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

static const char* TAG = "CLKOUT";

// MSB-first single byte on DATA, clocked by DATA-set + CLOCK rising/falling.
// No timing wait — the GPIO toggle latency on ESP32 (~100 ns at 240 MHz) is well
// inside the receiver's setup/hold window for all supported chipsets.
static inline void send_byte(int data_pin, int clock_pin, uint8_t b) {
    for (int i = 7; i >= 0; i--) {
        gpio_set_level((gpio_num_t)data_pin, (b >> i) & 1);
        gpio_set_level((gpio_num_t)clock_pin, 1);
        gpio_set_level((gpio_num_t)clock_pin, 0);
    }
}

esp_err_t clocked_output_init(clocked_output_t* out, int data_pin, int clock_pin,
                              uint16_t n_pixels, uint8_t channels) {
    if (!out || data_pin < 0 || clock_pin < 0 || data_pin == clock_pin) {
        return ESP_ERR_INVALID_ARG;
    }
    gpio_config_t gc = {};
    gc.pin_bit_mask  = (1ULL << data_pin) | (1ULL << clock_pin);
    gc.mode          = GPIO_MODE_OUTPUT;
    gc.pull_up_en    = GPIO_PULLUP_DISABLE;
    gc.pull_down_en  = GPIO_PULLDOWN_DISABLE;
    gc.intr_type     = GPIO_INTR_DISABLE;
    gpio_config(&gc);
    gpio_set_level((gpio_num_t)data_pin, 0);
    gpio_set_level((gpio_num_t)clock_pin, 0);

    out->data_pin  = data_pin;
    out->clock_pin = clock_pin;
    out->n_pixels  = n_pixels;
    out->channels  = channels;
    out->buf       = (uint8_t*)calloc((size_t)n_pixels * channels, 1);
    if (!out->buf) {
        ESP_LOGE(TAG, "alloc failed (%u × %u bytes)", n_pixels, channels);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void clocked_output_write_raw(clocked_output_t* out, uint16_t idx, const uint8_t* src) {
    if (!out || !out->buf || idx >= out->n_pixels) return;
    memcpy(out->buf + (size_t)idx * out->channels, src, out->channels);
}

void clocked_output_flush(clocked_output_t* out, led_protocol_t protocol) {
    if (!out || !out->buf) return;
    const int d = out->data_pin;
    const int c = out->clock_pin;
    const uint16_t n = out->n_pixels;

    // Start frame: 4 zero bytes for both APA102/SK9822 and P9813
    for (int i = 0; i < 4; i++) send_byte(d, c, 0x00);

    if (protocol == LED_APA102 || protocol == LED_SK9822) {
        // APA102/SK9822 per-pixel: [0xE0|brightness, B, G, R]. Brightness fixed at 0x1F
        // (full) — per-pixel brightness scaling is already applied in the wire bytes.
        for (uint16_t px = 0; px < n; px++) {
            const uint8_t* p = out->buf + (size_t)px * out->channels;
            send_byte(d, c, 0xFF);     // 0xE0 | 0x1F
            send_byte(d, c, p[0]);
            send_byte(d, c, p[1]);
            send_byte(d, c, p[2]);
        }
        // End frame: ≥ n/2 bits of 1 (clocks the per-pixel brightness through the chain)
        int end_bytes = (n + 15) / 16;
        if (end_bytes < 1) end_bytes = 1;
        for (int i = 0; i < end_bytes; i++) send_byte(d, c, 0xFF);
    } else if (protocol == LED_P9813) {
        // P9813 per-pixel: [flag, B, G, R]. flag = 0xC0 | (~B[7:6] | ~G[7:6] | ~R[7:6])
        // packed as bit positions [5:4][3:2][1:0] respectively.
        for (uint16_t px = 0; px < n; px++) {
            const uint8_t* p = out->buf + (size_t)px * out->channels;
            uint8_t b = p[0], g = p[1], r = p[2];
            uint8_t flag = 0xC0
                | ((~b >> 6) & 0x03)
                | ((~g >> 4) & 0x0C)
                | ((~r >> 2) & 0x30);
            send_byte(d, c, flag);
            send_byte(d, c, b);
            send_byte(d, c, g);
            send_byte(d, c, r);
        }
        // End frame: 4 zero bytes
        for (int i = 0; i < 4; i++) send_byte(d, c, 0x00);
    }
    // Other protocols: leave DATA/CLOCK low (start frame already sent — harmless reset)
}

void clocked_output_clear(clocked_output_t* out) {
    if (!out || !out->buf) return;
    memset(out->buf, 0, (size_t)out->n_pixels * out->channels);
}

void clocked_output_deinit(clocked_output_t* out) {
    if (!out) return;
    if (out->buf) { free(out->buf); out->buf = nullptr; }
    out->n_pixels = 0;
    out->channels = 0;
}
