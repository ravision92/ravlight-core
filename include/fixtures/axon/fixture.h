#pragma once
#ifdef RAVLIGHT_FIXTURE_AXON
#include <stdint.h>
#include "core/output_config.h"

// Axon optionally drives 1-N WS281x / PWM / relay LED outputs alongside the
// DMX-out bridge — enabled when the board file defines LED output pins.
// Config + render reuse the shared led_output_cfg_t (same struct as Elyon
// and Orion's optional LED outputs).
#if defined(HW_LED_OUTPUT_COUNT) && (HW_LED_OUTPUT_COUNT > 0)
#define AXON_HAS_LED 1
#define AXON_NUM_LED_OUTPUTS HW_LED_OUTPUT_COUNT
#endif

// Identity
#define AXON_FIXTURE_NAME    "Axon"
#define PROJECT_NAME         "Axon"
#define FIXTURE_STATUS       "alpha"

// ── DMX-out (network → RS-485 bridge) ────────────────────────────────────────
//
// Axon's primary job is a network-to-wire DMX bridge — but this is already a
// core feature, not Axon-specific code: when dmxConfig.dmxOutputEnabled is
// true and dmxConfig.dmxInput is ArtNet/sACN, the core mirrors the universe
// at dmxConfig.startUniverse into the legacy dmxBuffer[] and pushes it out
// the RS-485 port via esp_dmx on every render frame. Axon just sets
// dmxOutputEnabled = true by default and presents the universe selector
// front-and-centre in the fixture UI.
//
// The build env must enable RAVLIGHT_MODULE_DMX_PHYSICAL.

// ── Persistent config — serialised under config["fixture"] ───────────────────

struct AxonConfig {
#ifdef AXON_HAS_LED
    // Optional WS281x / PWM / relay LED outputs — Elyon-style per-output
    // config, persisted in NVS. Each output independently picks its
    // universe / start channel / protocol from the pool, identical to
    // how Orion's optional LED outputs work.
    led_output_cfg_t ledOutputs[AXON_NUM_LED_OUTPUTS];
#endif
    // No Axon-specific bridge config: the universe pushed out RS-485 is
    // dmxConfig.startUniverse, gated by dmxConfig.dmxOutputEnabled. Both
    // already live in DmxConfig and are exposed via the core DMX panel.
};

extern AxonConfig axonConfig;

#ifdef AXON_HAS_LED
// Start a white-wipe identification on one LED output (no-op if disabled).
void axonHighlightLed(int idx);
#endif

#endif // RAVLIGHT_FIXTURE_AXON
