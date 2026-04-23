#ifndef TEMP_SENSOR_H
#define TEMP_SENSOR_H

#ifdef RAVLIGHT_MODULE_TEMP

void initTemperatureSensor();
float readTemperature();
void updateTemperature();

#endif // RAVLIGHT_MODULE_TEMP
#endif // TEMP_SENSOR_H
