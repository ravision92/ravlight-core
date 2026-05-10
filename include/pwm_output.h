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
// channel: LEDC channel (0–7, LEDC_LOW_SPEED_MODE)
// timer:   LEDC timer (0–3); channels sharing a timer must use the same frequency
// freq_hz: PWM frequency in Hz (100–20000 typical)
bool pwm_output_init(pwm_output_t* p, int gpio, ledc_channel_t channel,
                     ledc_timer_t timer, uint32_t freq_hz);

// Set brightness 0–255. curve: 0=linear, 1=quadratic γ2.0, 2=cubic γ3.0
void pwm_output_set(pwm_output_t* p, uint8_t value, uint8_t curve);

// Set brightness 0–65535 (16-bit DMX, MSB+LSB combined). Same curve options.
void pwm_output_set16(pwm_output_t* p, uint16_t value, uint8_t curve);

void pwm_output_deinit(pwm_output_t* p);
