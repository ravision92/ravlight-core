#ifdef RAVLIGHT_MODULE_TEMP

#include "config.h"

float SensTemp = 0.0;

// ── LM35 — analog ADC (rev2.2 and boards with RAVLIGHT_HAS_TEMP_ANALOG) ──────

#ifdef RAVLIGHT_HAS_TEMP_ANALOG

#include <Arduino.h>
#include "esp_adc_cal.h"

static esp_adc_cal_characteristics_t s_adcChars;

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

// ── TMP102 — I2C (XDMX v3 and boards with RAVLIGHT_HAS_TEMP_I2C) ─────────────

#elif defined(RAVLIGHT_HAS_TEMP_I2C)

#include <Wire.h>
#include "esp_log.h"

static const char* TAG = "TEMP";

void initTemperatureSensor() {
    Wire.begin(HW_PIN_I2C_SDA, HW_PIN_I2C_SCL);
    // Write pointer register = 0x00 (temperature register) once at init
    Wire.beginTransmission(HW_TMP102_ADDR);
    Wire.write(0x00);
    Wire.endTransmission();
    ESP_LOGI(TAG, "TMP102 init at I2C addr 0x%02X (SDA=%d SCL=%d)",
             HW_TMP102_ADDR, HW_PIN_I2C_SDA, HW_PIN_I2C_SCL);
}

float readTemperature() {
    Wire.requestFrom((uint8_t)HW_TMP102_ADDR, (uint8_t)2);
    if (Wire.available() < 2) return SensTemp;  // return last known on bus error
    uint8_t msb = Wire.read();
    uint8_t lsb = Wire.read();
    // 12-bit two's complement: MSB[7:0] = bits[11:4], LSB[7:4] = bits[3:0]
    int16_t raw = ((int16_t)msb << 4) | (lsb >> 4);
    // Sign-extend 12-bit value
    if (raw & 0x800) raw |= 0xF000;
    float temp = raw * 0.0625f;
    if (temp < -40.0f) temp = -40.0f;
    if (temp > 125.0f) temp = 125.0f;
    return roundf(temp * 10.0f) / 10.0f;
}

void updateTemperature() {
    static uint32_t prevMs = 0;
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if (now - prevMs >= 10000) {
        prevMs = now;
        SensTemp = readTemperature();
        ESP_LOGI(TAG, "%.1f C", SensTemp);
    }
}

#else
#error "RAVLIGHT_MODULE_TEMP requires either RAVLIGHT_HAS_TEMP_ANALOG or RAVLIGHT_HAS_TEMP_I2C in the board file"
#endif

#endif // RAVLIGHT_MODULE_TEMP
