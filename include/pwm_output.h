#pragma once
#include <driver/ledc.h>
#include <stdint.h>

typedef struct {
    ledc_channel_t channel;
    ledc_timer_t   timer;
    ledc_mode_t    speed_mode;
    uint32_t       duty_max;   // (1 << resolution_bits) - 1
} pwm_output_t;

// Initialize a PWM output on the given GPIO pin.
// channel_idx: global output index 0–15.
//   0–7  → LEDC_LOW_SPEED_MODE,  LEDC channel (idx % 8), timer (idx % 4)
//   8–15 → LEDC_HIGH_SPEED_MODE, LEDC channel (idx % 8), timer (idx % 4)
// Channels sharing a timer group ({0,4},{1,5},{2,6},{3,7} in each speed mode)
// must use the same frequency.
// freq_hz: PWM frequency in Hz (100–20000 typical)
bool pwm_output_init(pwm_output_t* p, int gpio, uint8_t channel_idx, uint32_t freq_hz);

// Set brightness 0–255. curve: 0=linear, 1=quadratic γ2.0, 2=cubic γ3.0
void pwm_output_set(pwm_output_t* p, uint8_t value, uint8_t curve);

// Set brightness 0–65535 (16-bit DMX, MSB+LSB combined). Same curve options.
void pwm_output_set16(pwm_output_t* p, uint16_t value, uint8_t curve);

void pwm_output_deinit(pwm_output_t* p);
