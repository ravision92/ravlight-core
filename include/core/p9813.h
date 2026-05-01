#pragma once
// P9813 LED driver — bit-bang SPI, start/end frame protocol.
// One p9813_t = one independent chain of P9813 pixels.
// Veyron: 1 instance, 2 pixels (driving 6 COB white LEDs).

#include <stdint.h>
#include "driver/gpio.h"
#include "esp_err.h"

typedef struct {
    gpio_num_t pin_data;
    gpio_num_t pin_clk;
    uint8_t*   buf;       // RGB pixel buffer [R0,G0,B0, R1,G1,B1, ...]
    uint8_t    n_pixels;
} p9813_t;

// pin_data / pin_clk  — GPIO numbers
// n_pixels            — number of P9813 pixels in the chain
esp_err_t p9813_init(p9813_t* dev, int pin_data, int pin_clk, uint8_t n_pixels);

void p9813_set_pixel(p9813_t* dev, uint8_t idx, uint8_t r, uint8_t g, uint8_t b);
void p9813_flush(p9813_t* dev);   // transmits start frame + pixel frames + end frame
void p9813_clear(p9813_t* dev);   // zeros buffer (call flush to output)
void p9813_deinit(p9813_t* dev);
