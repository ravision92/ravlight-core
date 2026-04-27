#pragma once
#ifdef RAVLIGHT_FIXTURE_ELYON
#include <stdint.h>
#include "core/dmx_patch.h"
#include "fixtures/elyon/fixture_ids.h"
#include "fixtures/elyon/output_config.h"

// Elyon personalities — active outputs vary by personality.
// pixel_count per output is set at runtime via elyon_output_cfg_t.
// ch_count in personality_t is 0 (runtime-variable — computed from output configs).

// ── P1: 1 output active ──────────────────────────────────────────────────────
static const dmx_channel_t ELYON_PERS_1_CH[] = {
    { CH_PIXEL_RGB, ID_ELYON_OUT0_PIXELS, 0, "Output 0", 0, 0, 1 }, // count set at runtime
};

// ── P2: 2 outputs active ─────────────────────────────────────────────────────
static const dmx_channel_t ELYON_PERS_2_CH[] = {
    { CH_PIXEL_RGB, ID_ELYON_OUT0_PIXELS, 0, "Output 0", 0, 0, 1 },
    { CH_PIXEL_RGB, ID_ELYON_OUT1_PIXELS, 1, "Output 1", 0, 0, 1 },
};

// ── P3: 4 outputs active ─────────────────────────────────────────────────────
static const dmx_channel_t ELYON_PERS_4_CH[] = {
    { CH_PIXEL_RGB, ID_ELYON_OUT0_PIXELS, 0, "Output 0", 0, 0, 1 },
    { CH_PIXEL_RGB, ID_ELYON_OUT1_PIXELS, 1, "Output 1", 0, 0, 1 },
    { CH_PIXEL_RGB, ID_ELYON_OUT2_PIXELS, 2, "Output 2", 0, 0, 1 },
    { CH_PIXEL_RGB, ID_ELYON_OUT3_PIXELS, 3, "Output 3", 0, 0, 1 },
};

// ── P4: 8 outputs active ─────────────────────────────────────────────────────
static const dmx_channel_t ELYON_PERS_8_CH[] = {
    { CH_PIXEL_RGB, ID_ELYON_OUT0_PIXELS, 0, "Output 0", 0, 0, 1 },
    { CH_PIXEL_RGB, ID_ELYON_OUT1_PIXELS, 1, "Output 1", 0, 0, 1 },
    { CH_PIXEL_RGB, ID_ELYON_OUT2_PIXELS, 2, "Output 2", 0, 0, 1 },
    { CH_PIXEL_RGB, ID_ELYON_OUT3_PIXELS, 3, "Output 3", 0, 0, 1 },
    { CH_PIXEL_RGB, ID_ELYON_OUT4_PIXELS, 4, "Output 4", 0, 0, 1 },
    { CH_PIXEL_RGB, ID_ELYON_OUT5_PIXELS, 5, "Output 5", 0, 0, 1 },
    { CH_PIXEL_RGB, ID_ELYON_OUT6_PIXELS, 6, "Output 6", 0, 0, 1 },
    { CH_PIXEL_RGB, ID_ELYON_OUT7_PIXELS, 7, "Output 7", 0, 0, 1 },
};

static const personality_t ELYON_PERSONALITIES[] = {
    { "1 Output",  0, ELYON_PERS_1_CH, 1 },
    { "2 Outputs", 0, ELYON_PERS_2_CH, 2 },
    { "4 Outputs", 0, ELYON_PERS_4_CH, 4 },
    { "8 Outputs", 0, ELYON_PERS_8_CH, 8 },
};

#define ELYON_NUM_PERSONALITIES  (sizeof(ELYON_PERSONALITIES) / sizeof(ELYON_PERSONALITIES[0]))

#endif // RAVLIGHT_FIXTURE_ELYON
