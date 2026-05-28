#pragma once
// WS2812-family LED driver via ESP32 I2S0 in LCD-parallel mode.
// Drives up to 8 channels simultaneously. DMA is chunked: only
// I2S_PAR_BUFS × I2S_PAR_CHUNK_PX pixels are in DRAM at any time —
// memory is O(CHUNK_PIXELS), independent of strip length.
//
// Typical flow (from render task):
//   i2s_par_set_source(ch, buf, n_pixels, bytes_pp);  // once per config change
//   i2s_par_trigger_frame();                           // start async DMA
//   i2s_par_wait_done();                               // block until reset pulse done

#include <stdint.h>
#include "esp_err.h"

#define I2S_PAR_MAX_CH    8   // maximum parallel WS channels
#define I2S_PAR_CHUNK_PX  16  // pixels per DMA chunk (tune for memory vs ISR overhead)
#define I2S_PAR_BUFS      3   // ring depth — must be ≥ 3 to guarantee refill window

typedef struct {
    int      gpio_pins[I2S_PAR_MAX_CH];  // GPIO per channel; -1 = channel unused
    uint8_t  n_channels;                  // number of entries in gpio_pins
    uint16_t max_pixels_per_ch;          // upper bound on strip length (caps alloc)
} i2s_par_cfg_t;

// Initialise I2S peripheral, GPIO matrix and allocate DMA resources.
// Called once at startup. Spawns encoder_task (core 1, prio 22).
esp_err_t i2s_par_init(const i2s_par_cfg_t* cfg);

// Register the pixel source buffer for one channel.
//   buf       — wire-order bytes already color-corrected by caller
//   n_pixels  — active strip length
//   bytes_pp  — 3 for RGB, 4 for RGBW
void i2s_par_set_source(uint8_t ch, const uint8_t* buf, uint16_t n_pixels,
                         uint8_t bytes_pp);

// Start asynchronous DMA transmission of the current source buffers.
// Returns immediately; encoding happens in encoder_task.
void i2s_par_trigger_frame(void);

// Block until the full frame AND the WS2812 reset pulse have been transmitted.
void i2s_par_wait_done(void);
