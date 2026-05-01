#ifndef RUNTIME_H
#define RUNTIME_H

void   initRuntime();
void   updateRuntime();
String getSerialNumber();

extern uint32_t totalRuntime;
extern uint32_t currentRuntime;

#endif
