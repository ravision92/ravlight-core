#ifndef DMX_MANAGER_H
#define DMX_MANAGER_H

#include <stdint.h>

#define DMX_BUFFER_SIZE 513

extern uint8_t dmxBuffer[DMX_BUFFER_SIZE];

// Core DMX functions (ArtNet + sACN input, dispatcher, status LED)
void initDmxInputs();
void initArtnet();
void initE131();
void receiveDmxData();
void getArtnetDMX();
void get131DMX();
void DMXLedRun();

// Physical RS-485 DMX module
#ifdef RAVLIGHT_MODULE_DMX_PHYSICAL
void initWiredDmx();
void getWiredDMX();
void sendDmxData();
#endif // RAVLIGHT_MODULE_DMX_PHYSICAL

// Live reinit helpers — apply config changes without MCU restart
void reinitDMXInput();
void reinitUniverse(uint16_t universe);
void reinitDMXOutput(bool enable);

#endif // DMX_MANAGER_H
