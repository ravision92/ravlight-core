#include "core/p9813.h"
#include "esp_rom_sys.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

static const char* TAG = "P9813";

static inline void clk_pulse(p9813_t* dev) {
    gpio_set_level(dev->pin_clk, 0);
    esp_rom_delay_us(1);
    gpio_set_level(dev->pin_clk, 1);
    esp_rom_delay_us(1);
}

static void send_byte(p9813_t* dev, uint8_t b) {
    for (int i = 7; i >= 0; i--) {
        gpio_set_level(dev->pin_data, (b >> i) & 1);
        clk_pulse(dev);
    }
}

// P9813 wire format per pixel: [flag, B, G, R]
// flag = 0xFF ^ (~B[7:6] | ~G[5:4] | ~R[3:2])
static void send_pixel_frame(p9813_t* dev, uint8_t r, uint8_t g, uint8_t b) {
    uint8_t flag = 0xFF ^ ((b >> 6) | ((g >> 4) & 0x0C) | ((r >> 2) & 0x30));
    send_byte(dev, flag);
    send_byte(dev, b);
    send_byte(dev, g);
    send_byte(dev, r);
}

esp_err_t p9813_init(p9813_t* dev, int pin_data, int pin_clk, uint8_t n_pixels) {
    dev->pin_data = (gpio_num_t)pin_data;
    dev->pin_clk  = (gpio_num_t)pin_clk;
    dev->n_pixels = n_pixels;
    dev->buf      = (uint8_t*)calloc(n_pixels * 3, 1);
    if (!dev->buf) {
        ESP_LOGE(TAG, "alloc failed");
        return ESP_ERR_NO_MEM;
    }

    gpio_config_t io = {};
    io.pin_bit_mask = (1ULL << pin_data) | (1ULL << pin_clk);
    io.mode         = GPIO_MODE_OUTPUT;
    io.pull_up_en   = GPIO_PULLUP_DISABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type    = GPIO_INTR_DISABLE;
    gpio_config(&io);

    gpio_set_level(dev->pin_data, 0);
    gpio_set_level(dev->pin_clk, 0);

    ESP_LOGI(TAG, "data=%d clk=%d n=%d", pin_data, pin_clk, n_pixels);
    return ESP_OK;
}

void p9813_set_pixel(p9813_t* dev, uint8_t idx, uint8_t r, uint8_t g, uint8_t b) {
    if (idx >= dev->n_pixels) return;
    dev->buf[idx * 3]     = r;
    dev->buf[idx * 3 + 1] = g;
    dev->buf[idx * 3 + 2] = b;
}

void p9813_flush(p9813_t* dev) {
    for (int i = 0; i < 4; i++) send_byte(dev, 0x00);
    for (int i = 0; i < dev->n_pixels; i++) {
        send_pixel_frame(dev, dev->buf[i * 3], dev->buf[i * 3 + 1], dev->buf[i * 3 + 2]);
    }
    for (int i = 0; i < 4; i++) send_byte(dev, 0x00);
}

void p9813_clear(p9813_t* dev) {
    memset(dev->buf, 0, dev->n_pixels * 3);
}

void p9813_deinit(p9813_t* dev) {
    free(dev->buf);
    dev->buf = NULL;
}
