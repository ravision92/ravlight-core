#ifndef DMX_MANAGER_H
#define DMX_MANAGER_H

#include <stdint.h>
#include "freertos/semphr.h"

#define DMX_BUFFER_SIZE   513
#define DMX_MAX_UNIVERSES  48   // max universes in pool (48 × 513 B ≈ 24 KB; 8 ch × 6 univ @ 1024 px RGB)

extern uint8_t dmxBuffer[DMX_BUFFER_SIZE];  // legacy single-universe buffer (startUniverse)
extern SemaphoreHandle_t dmxBufferMutex;
extern bool handleDMXenable;

// ── Universe pool API ────────────────────────────────────────────────────────
// Fixtures call registerDmxUniverse() in initFixture() BEFORE initDmxInputs().
// initDmxInputs() auto-registers startUniverse for single-universe fixtures.
void registerDmxUniverse(uint16_t universe);

// Returns pointer to channel data for a registered universe (1-indexed: [1]=ch1).
// Returns nullptr if universe not registered. Lock-free: returns the active
// half of the per-universe double buffer. The active buffer never gets
// written to except by an ArtSync swap, so render can iterate it without
// holding dmxBufferMutex.
const uint8_t* getUniverseData(uint16_t universe);

// Apply any ArtSync that arrived while a render frame was in flight. Render
// task calls this at the start of every frame — it briefly takes
// dmxBufferMutex, flips active_idx for every universe that ArtDMX dirtied,
// then releases. No-op if no ArtSync is pending.
void dmxApplyPendingSwap();

// Cumulative counts of received sync packets (diagnostic / /stats).
//   artsyncPacketCount()  — Art-Net 4 ArtSync (opcode 0x5200)
//   sacnsyncPacketCount() — E1.31 Extended Synchronization (frame vector 0x00000001)
// Both drive the same render-task pending-swap mechanism.
uint32_t artsyncPacketCount();
uint32_t sacnsyncPacketCount();

// Returns millis() of the most recent frame received for this universe,
// or 0 if the universe has never received data. Used by fixtures to gate
// DMX-loss watchdogs on the SPECIFIC universe they listen on, instead of
// the global "any DMX traffic" signal.
uint32_t getUniverseLastSeen(uint16_t universe);

// Synthetic-source injection (test pattern, future replay etc.). Writes `length`
// bytes (1-indexed channels) into the registered universe; no-op if not registered.
// Takes dmxBufferMutex internally; do not hold it when calling.
void injectDmxUniverse(uint16_t universe, const uint8_t* src, uint16_t length);

// Iterate over universes the active fixture has registered. Used by the
// effects engine and recorder to avoid blind-probing every possible
// universe id (which still takes the mutex per call even when there's no
// matching pool slot).
uint8_t  dmxUniverseCount();
uint16_t dmxUniverseAt(uint8_t idx);   // 0 if idx out of range

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
