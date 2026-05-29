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
uint32_t artnetPacketCount(void);  // cumulative received ArtDMX packets (diagnostic)
void initE131();
void receiveDmxData();
void get131DMX();
void DMXLedRun();

// True if a DMX frame (any source) was received within the last activity window
// (~1.5 s). Shared status used by fixtures and UI banners.
bool dmxIsActive();

// Physical RS-485 DMX port 1 (UART1 / GPIO33/35)
#ifdef RAVLIGHT_MODULE_DMX_PHYSICAL
void initWiredDmx();
void getWiredDMX();
void sendDmxData();
#endif

// Physical RS-485 DMX port 2 (UART0 / GPIO1/GPIO3); requires RAVLIGHT_DISABLE_SERIAL
#ifdef RAVLIGHT_MODULE_DMX_PHYSICAL_2
void initWiredDmx2();
void getWiredDMX2();
#endif

// Live reinit helpers — apply config changes without MCU restart
void reinitDMXInput();
void reinitUniverse(uint16_t universe);
void reinitDMXOutput(bool enable);

#endif // DMX_MANAGER_H
