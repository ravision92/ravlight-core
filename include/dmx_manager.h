#ifndef DMX_MANAGER_H
#define DMX_MANAGER_H

#include <stdint.h>
#include "freertos/semphr.h"

#define DMX_BUFFER_SIZE   513
#define DMX_MAX_UNIVERSES  32   // max universes in pool (32 × 515 B ≈ 16 KB)

extern uint8_t dmxBuffer[DMX_BUFFER_SIZE];  // legacy single-universe buffer (startUniverse)
extern SemaphoreHandle_t dmxBufferMutex;
extern bool handleDMXenable;

// ── Universe pool API ────────────────────────────────────────────────────────
// Fixtures call registerDmxUniverse() in initFixture() BEFORE initDmxInputs().
// initDmxInputs() auto-registers startUniverse for single-universe fixtures.
void registerDmxUniverse(uint16_t universe);

// Returns pointer to channel data for a registered universe (1-indexed: [1]=ch1).
// Returns nullptr if universe not registered. Caller must hold dmxBufferMutex.
const uint8_t* getUniverseData(uint16_t universe);

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
