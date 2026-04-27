#pragma once
#ifdef RAVLIGHT_FIXTURE_AXON
#include <stdint.h>
#include "core/dmx_patch.h"
#include "fixtures/axon/fixture_ids.h"

// Axon section indices
#define AXON_SEC_OUTPUT  0   // base = DMX output start address

// ── P1: Passthrough (512 ch) ─────────────────────────────────────────────────
static const dmx_channel_t AXON_PERS_1_CH[] = {
    { CH_UART_OUT, ID_AXON_PASSTHROUGH, AXON_SEC_OUTPUT, "DMX Passthrough", 0, 0, 512 },
};

// ── P2: ArtNet/sACN Bridge (512 ch) ──────────────────────────────────────────
static const dmx_channel_t AXON_PERS_2_CH[] = {
    { CH_UART_OUT, ID_AXON_UNIVERSE_A, AXON_SEC_OUTPUT, "Universe A Out", 0, 0, 512 },
};

// ── P3: HTP Merge (2 universes → 1 RS-485 output) ────────────────────────────
static const dmx_channel_t AXON_PERS_3_CH[] = {
    { CH_UART_OUT, ID_AXON_UNIVERSE_A, AXON_SEC_OUTPUT, "Universe A",    0, 0, 512 },
    { CH_UART_OUT, ID_AXON_UNIVERSE_B, AXON_SEC_OUTPUT, "Universe B",    0, 0, 512 },
};

static const personality_t AXON_PERSONALITIES[] = {
    { "Passthrough",   512, AXON_PERS_1_CH, 1 },
    { "ArtNet Bridge", 512, AXON_PERS_2_CH, 1 },
    { "HTP Merge",     512, AXON_PERS_3_CH, 2 },
};

#define AXON_NUM_PERSONALITIES  (sizeof(AXON_PERSONALITIES) / sizeof(AXON_PERSONALITIES[0]))

#endif // RAVLIGHT_FIXTURE_AXON
