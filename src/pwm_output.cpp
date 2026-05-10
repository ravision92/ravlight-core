#include "pwm_output.h"
#include <esp_log.h>

static const char* TAG = "PWM";

// Auto-calculate resolution: floor(log2(80MHz / freq_hz)), clamped [8,16]
static uint8_t calc_resolution(uint32_t freq_hz) {
    if (freq_hz == 0) freq_hz = 1;
    uint8_t bits = 0;
    uint32_t div = 80000000UL / freq_hz;
    while (div > 1) { div >>= 1; bits++; }
    if (bits < 8)  bits = 8;
    if (bits > 16) bits = 16;
    return bits;
}

bool pwm_output_init(pwm_output_t* p, int gpio, ledc_channel_t channel,
                     ledc_timer_t timer, uint32_t freq_hz) {
    p->channel    = channel;
    p->timer      = timer;
    p->speed_mode = LEDC_LOW_SPEED_MODE;

    uint8_t bits = calc_resolution(freq_hz);
    p->duty_max  = (1u << bits) - 1;

    ledc_timer_config_t tcfg = {};
    tcfg.speed_mode      = LEDC_LOW_SPEED_MODE;
    tcfg.timer_num       = timer;
    tcfg.duty_resolution = (ledc_timer_bit_t)bits;
    tcfg.freq_hz         = freq_hz;
    tcfg.clk_cfg         = LEDC_AUTO_CLK;
    esp_err_t err = ledc_timer_config(&tcfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "timer%d config failed: %d", timer, err);
        return false;
    }

    ledc_channel_config_t ccfg = {};
    ccfg.gpio_num   = gpio;
    ccfg.speed_mode = LEDC_LOW_SPEED_MODE;
    ccfg.channel    = channel;
    ccfg.timer_sel  = timer;
    ccfg.duty       = 0;
    ccfg.hpoint     = 0;
    err = ledc_channel_config(&ccfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ch%d config failed: %d", channel, err);
        return false;
    }

    ESP_LOGI(TAG, "ch%d gpio%d timer%d %uHz res=%ubits duty_max=%lu",
             channel, gpio, timer, (unsigned)freq_hz, bits, (unsigned long)p->duty_max);
    return true;
}

void pwm_output_set(pwm_output_t* p, uint8_t value, uint8_t curve) {
    uint32_t duty;
    switch (curve) {
        case 1:  // quadratic γ2.0
            duty = (uint32_t)value * value * p->duty_max / 65025u;
            break;
        case 2:  // cubic γ3.0
            duty = (uint32_t)((uint64_t)value * value * value * p->duty_max / 16581375ULL);
            break;
        default: // linear
            duty = (uint32_t)value * p->duty_max / 255u;
            break;
    }
    ledc_set_duty(p->speed_mode, p->channel, duty);
    ledc_update_duty(p->speed_mode, p->channel);
}

void pwm_output_set16(pwm_output_t* p, uint16_t value, uint8_t curve) {
    float f = value / 65535.0f;
    float shaped;
    switch (curve) {
        case 1:  shaped = f * f;      break;  // quadratic γ2.0
        case 2:  shaped = f * f * f;  break;  // cubic γ3.0
        default: shaped = f;          break;  // linear
    }
    uint32_t duty = (uint32_t)(shaped * (float)p->duty_max);
    ledc_set_duty(p->speed_mode, p->channel, duty);
    ledc_update_duty(p->speed_mode, p->channel);
}

void pwm_output_deinit(pwm_output_t* p) {
    ledc_stop(p->speed_mode, p->channel, 0);
}
