#pragma once
// WS2811/WS2812-family LED strip driver via IDF 4.4 RMT.
// One led_output_t = one RMT channel = one independent strip.
// Veyron: 1 instance on RMT_CHANNEL_0.
// Elyon:  8 instances on RMT_CHANNEL_0..7.

#include <stdint.h>
#include "driver/rmt.h"

typedef struct {
    rmt_channel_t  channel;
    uint8_t*       buf;        // pixel buffer: channels bytes per pixel, wire order
    uint16_t       n_pixels;
    uint8_t        channels;   // bytes per pixel: 3 (RGB) or 4 (RGBW)
    // No per-output RMT item buffer — channels flush sequentially so a single
    // shared global buffer is reused (see led_output.cpp g_rmt_items).
} led_output_t;

// gpio_num   — output GPIO pin
// n_pixels   — strip length
// channel    — RMT_CHANNEL_0..7; each strip needs its own channel
// mem_blocks — RMT memory blocks (1–8); use 4 for single strip, 1 for 8-ch Elyon
// channels   — bytes per pixel: 3 for RGB strips, 4 for RGBW (SK6812, WS2814)
esp_err_t led_output_init(led_output_t* out, int gpio_num, uint16_t n_pixels,
                           rmt_channel_t channel, uint8_t mem_blocks, uint8_t channels);

// Write a single RGB pixel (RGB strips only — assumes channels=3).
void led_output_set_pixel(led_output_t* out, uint16_t idx, uint8_t r, uint8_t g, uint8_t b);

// Write pre-mapped wire-order bytes (works for both RGB and RGBW).
// src must contain exactly out->channels bytes already in the correct wire order.
void led_output_write_raw(led_output_t* out, uint16_t idx, const uint8_t* src);

void led_output_flush(led_output_t* out);
void led_output_clear(led_output_t* out);
void led_output_deinit(led_output_t* out);
