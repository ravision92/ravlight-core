#ifdef RAVLIGHT_MODULE_TEMP

#include <Arduino.h>
#include "esp_adc_cal.h"
#include "settings.h"
uint32_t readAndCalibrateADC(int rawADCValue);

unsigned long previousTempMillis = 0;
const long tempInterval = 10000;

float SensTemp = 0.0;

void initTemperatureSensor() {
    // LM35 uses analog read; no explicit pinMode needed for ADC pins
}

float readTemperature() {
    int sensorValue = analogRead(HW_PIN_TEMP);
    float voltage = readAndCalibrateADC(sensorValue);
    float temperature = voltage / 10;  // LM35: 10 mV per degree Celsius
    return round(temperature * 10) / 10.0;
}

void updateTemperature() {
    unsigned long currentMillis = millis();
    if (currentMillis - previousTempMillis >= tempInterval) {
        previousTempMillis = currentMillis;
        SensTemp = readTemperature();
        Serial.print("[TEMP] Temperature: ");
        Serial.print(SensTemp);
        Serial.println(" C");
    }
}

uint32_t readAndCalibrateADC(int rawADCValue) {
    esp_adc_cal_characteristics_t adcCharacteristics;
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &adcCharacteristics);
    return esp_adc_cal_raw_to_voltage(rawADCValue, &adcCharacteristics);
}

#endif // RAVLIGHT_MODULE_TEMP
