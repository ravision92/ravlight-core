#pragma once
#include <ArduinoJson.h>

// Unified fixture interface — each fixture implements all of these in its own
// fixtures/<name>/ files.  No #ifdef RAVLIGHT_FIXTURE_* needed in core code.

// Config (fixtures/<name>/fixture_config.cpp)
void fixtureConfigDefaults();
void fixtureConfigSerialize(JsonObject& fix);
void fixtureConfigDeserialize(const JsonObject& fix);
// After fixtureConfigDeserialize updates the in-memory config, fixtures
// push the new values to runtime state (motor driver, RMT init, etc.) here.
// Returns true if a device restart is still required for changes that
// cannot be applied live.
bool fixtureApplyLive();
void fixtureGetDmxMap(JsonObject& map);

// DMX (fixtures/<name>/dmx_fixture.cpp)
void initFixture();
void handleDMX();
void startDMX();
void stopDMX();
void fixtureHighlight();   // visual identify pulse; no-op on fixtures that don't support it

// Built-in effects engine target — describes a contiguous run of pixel
// channels the fixture wants the effects renderer to paint into. Used so
// the engine paints ONLY real pixels and never spills random RGB bytes
// into function channels (strobe, dim curve, motor position, etc.).
//   universe       — 0-based universe id (matches Art-Net wire format)
//   dmx_start      — 1-based channel inside the universe of the first
//                    pixel's first byte
//   pixel_count    — number of logical pixels in the run
//   ch_per_pixel   — 3 for RGB strips, 4 for RGBW
typedef struct {
    uint16_t universe;
    uint16_t dmx_start;
    uint16_t pixel_count;
    uint8_t  ch_per_pixel;
} fx_target_t;

// Fixture exposes its render targets to the effects engine. Writes up to
// `max` entries into `out`, returns the actual count. Effects engine
// treats the concatenation of all targets as one virtual strip so an
// effect like Chase sweeps cleanly across multiple LED outputs / accent
// rows / etc. Default no-op fixture (or one that doesn't want effects on
// any channel) returns 0.
uint8_t fixtureGetEffectTargets(fx_target_t* out, uint8_t max);

// Overlay fixture-specific function channels (white, strobe, dim curves,
// etc.) on top of the per-pixel render buffer for one universe. Called
// AFTER the renderer has filled the pixel ranges, so anything the hook
// writes wins. No-op default for fixtures without function channels.
//   buf       — 513-byte buffer, 1-indexed channels (buf[1] = ch1)
//   universe  — 0-based universe id we're currently emitting
void fixtureApplyEffectFunctions(uint8_t* buf, uint16_t universe);
