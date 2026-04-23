#ifndef RUNTIMENVS_H
#define RUNTIMENVS_H


void saveToNVS(const char* key, uint32_t value);
uint32_t loadFromNVS(const char* key, uint32_t defaultValue);
void updateRuntime();
void initNVS();
void setSerialNumber(const char* serial);
String getSerialNumber();

extern uint32_t totalRuntime;
extern uint32_t currentRuntime;

#endif
