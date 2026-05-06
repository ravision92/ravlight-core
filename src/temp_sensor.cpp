#ifdef RAVLIGHT_MODULE_TEMP

#include <Arduino.h>
#include "esp_adc_cal.h"
#include "config.h"

static esp_adc_cal_characteristics_t s_adcChars;

float SensTemp = 0.0;

void initTemperatureSensor() {
    analogSetPinAttenuation(HW_PIN_TEMP, ADC_11db);
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &s_adcChars);
}

float readTemperature() {
    // Average 32 samples to reject LED switching noise (800 kHz RMT on adjacent pins)
    uint32_t sum = 0;
    for (int i = 0; i < 32; i++) {
        sum += analogRead(HW_PIN_TEMP);
    }
    uint32_t mV = esp_adc_cal_raw_to_voltage(sum / 32, &s_adcChars);
    float temp = (float)mV / 10.0f;  // LM35: 10 mV/°C; mV from calibration API
    if (temp < 0.0f)   temp = 0.0f;
    if (temp > 100.0f) temp = 100.0f;
    return roundf(temp * 10.0f) / 10.0f;
}

void updateTemperature() {
    static unsigned long prevMs = 0;
    unsigned long now = millis();
    if (now - prevMs >= 10000) {
        prevMs = now;
        SensTemp = readTemperature();
        Serial.printf("[TEMP] %.1f C\n", SensTemp);
    }
}

#endif // RAVLIGHT_MODULE_TEMP
