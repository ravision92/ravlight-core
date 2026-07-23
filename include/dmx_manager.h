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
uint32_t sacnPacketCount(void);    // cumulative received E1.31 data packets
uint32_t wiredPacketCount(void);   // cumulative wired RS-485 DMX RX frames
uint32_t injectPacketCount(void);  // cumulative injectDmxUniverse() calls (effects, recorder, test pattern)

// Source frame rate, in Hz, computed over a 250 ms sliding window from the
// combined ArtDMX + sACN packet stream divided by the number of universes
// currently receiving traffic — same maths as the OLED status display
// (single point of truth, used by both OLED and the /api/status response).
// Caller must invoke tickDmxFps() from the main loop to keep it fresh.
uint16_t dmxSourceFps(void);
void     tickDmxFps(void);
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

// Live RDM personality index (1-based, matches esp_dmx numbering), reflecting
// whatever a console last set via RDM_PID_DMX_PERSONALITY SET (or 1 at boot).
// Fixtures with >1 real personality (see fixtureGetRdmPersonalities()) should
// poll this from their own handleDMX() and apply it to their runtime state,
// so an RDM personality change actually takes effect instead of being purely
// cosmetic metadata.
uint8_t dmxGetCurrentPersonality();

// Pushes a personality change back into esp_dmx's own RDM state (e.g. after
// a boot-time load or a web UI save) so dmxGetCurrentPersonality() doesn't
// keep reporting stale/default (1) forever. Without this, a fixture that
// only ever *reads* RDM's personality and never writes its own choice back
// gets that choice silently reverted on the very next handleDMX() call —
// esp_dmx defaults to personality 1 at dmx_driver_install() and nothing
// else ever moves it, so the poll-and-apply pattern above sees "RDM says 1,
// runtime says N" forever and forces the runtime back to 1.
void dmxSetCurrentPersonality(uint8_t personality_num);

// Live RDM_PID_DMX_START_ADDRESS value. esp_dmx auto-registers this PID with
// its own default GET/SET handler (independent of fixture-specific channel
// config), so a console SET here only updates esp_dmx's internal parameter —
// fixtures with their own addressing model (e.g. Veyron's per-section start
// addresses) must poll dmxGetStartAddress() themselves and apply it, same
// pattern as dmxGetCurrentPersonality(). dmxSetStartAddress() lets a fixture
// push its own address back (e.g. after a web UI change) so GET stays
// consistent with what the fixture is actually using.
uint16_t dmxGetStartAddress();
void     dmxSetStartAddress(uint16_t addr);
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
