#pragma once
// Bit-bang driver for 2-wire clocked LED chipsets (APA102 / SK9822 / P9813).
// Each output owns a DATA pin and a separate CLOCK pin. No timing constraint —
// the receiver clocks data in on the rising edge of CLOCK, so any bit-bang rate
// works. Sequential per-output (a single render-task thread bit-bangs all
// pixels of one output, then moves to the next).
//
// Logical buffer layout: `n_pixels × channels` bytes in already-permuted wire
// byte order (matching the led_output_t convention). Protocol-specific framing
// (start frame, per-pixel prefix byte, end frame) is added at flush time —
// the buffer never stores those bytes.

#include <stdint.h>
#include "esp_err.h"
#include "core/output_config.h"

typedef struct {
    int      data_pin;
    int      clock_pin;
    uint8_t* buf;        // n_pixels × channels bytes, wire byte order
    uint16_t n_pixels;
    uint8_t  channels;   // bytes per pixel stored in buf (3 for current chipsets)
} clocked_output_t;

// Configure DATA + CLOCK GPIOs as outputs, allocate the per-pixel buffer.
// Returns ESP_ERR_NO_MEM if buf allocation fails, ESP_ERR_INVALID_ARG for bad pins.
esp_err_t clocked_output_init(clocked_output_t* out, int data_pin, int clock_pin,
                              uint16_t n_pixels, uint8_t channels);

// Store `channels` bytes for pixel `idx` (wire byte order — caller has already
// applied color_order and brightness scaling, as for led_output_write_raw).
void clocked_output_write_raw(clocked_output_t* out, uint16_t idx, const uint8_t* src);

// Emit start frame + per-pixel framing + end frame on DATA/CLOCK.
// `protocol` selects the encoding: LED_APA102 / LED_SK9822 / LED_P9813.
// Blocking — returns when transmission has finished.
void clocked_output_flush(clocked_output_t* out, led_protocol_t protocol);

void clocked_output_clear(clocked_output_t* out);
void clocked_output_deinit(clocked_output_t* out);
