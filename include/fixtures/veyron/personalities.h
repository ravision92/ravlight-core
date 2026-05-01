#pragma once
#ifdef RAVLIGHT_FIXTURE_VEYRON
#include <stdint.h>
#include "core/dmx_patch.h"
#include "fixtures/veyron/fixture_ids.h"
#include "fixtures/veyron/fixture.h"

// Veyron section indices for patch_state_t.section_start[]
#define VEYRON_SEC_STRIP   0   // base = RGBWstartAddress
#define VEYRON_SEC_ACCENT  1   // base = WhStartAddress
#define VEYRON_SEC_STROBE  2   // base = strobeStartAddress

// Strobe power curve: interval_ms = 24.454 * rate^0.423, mapped to [MIN..MAX]
#define VEYRON_STROBE_CURVE_A   24.454f
#define VEYRON_STROBE_CURVE_B   0.423f
#define VEYRON_STROBE_RATE_MIN  2500    // ms — slowest strobe
#define VEYRON_STROBE_RATE_MAX  40      // ms — fastest strobe

// ── P1: Full Pixel + Strobe  (128 ch) ───────────────────────────────────────
// strip: 40×3 = 120ch | accent: 2×3 = 6ch | strobe: 2ch
static const dmx_channel_t PERS_1_CH[] = {
    { CH_PIXEL_RGB,    ID_STRIP_PIXELS,  VEYRON_SEC_STRIP,  "Strip Pixels",  0, 0, VEYRON_NUM_PIXELS_1 * 3 },
    { CH_ACCENT_RGB,   ID_ACCENT_PIXELS, VEYRON_SEC_ACCENT, "Accent Pixels", 0, 0, VEYRON_NUM_PIXELS_2 * 3 },
    { CH_STROBE,       ID_STROBE_STRIP,  VEYRON_SEC_STROBE, "Strobe Strip",  0, 1, 1 },
    { CH_STROBE,       ID_STROBE_ACCENT, VEYRON_SEC_STROBE, "Strobe Accent", 0, 1, 1 },
};

// ── P2: Full Pixel  (126 ch) ─────────────────────────────────────────────────
// strip: 40×3 = 120ch | accent: 2×3 = 6ch | no strobe
static const dmx_channel_t PERS_2_CH[] = {
    { CH_PIXEL_RGB,    ID_STRIP_PIXELS,  VEYRON_SEC_STRIP,  "Strip Pixels",  0, 0, VEYRON_NUM_PIXELS_1 * 3 },
    { CH_ACCENT_RGB,   ID_ACCENT_PIXELS, VEYRON_SEC_ACCENT, "Accent Pixels", 0, 0, VEYRON_NUM_PIXELS_2 * 3 },
};

// ── P3: Broadcast RGB + Strobe  (6 ch) ───────────────────────────────────────
// strip: 3ch cast | accent: 1ch white | strobe: 2ch
static const dmx_channel_t PERS_3_CH[] = {
    { CH_PIXEL_CAST,   ID_STRIP_CAST,    VEYRON_SEC_STRIP,  "Strip Color",   0, 0, 3 },
    { CH_ACCENT_WHITE, ID_ACCENT_WHITE,  VEYRON_SEC_ACCENT, "Accent White",  0, 0, 1 },
    { CH_STROBE,       ID_STROBE_STRIP,  VEYRON_SEC_STROBE, "Strobe Strip",  0, 1, 1 },
    { CH_STROBE,       ID_STROBE_ACCENT, VEYRON_SEC_STROBE, "Strobe Accent", 0, 1, 1 },
};

// ── P4: Mirror + Strobe  (68 ch) ─────────────────────────────────────────────
// strip: 20×3 = 60ch mirrored to 40px | accent: 2×3 = 6ch | strobe: 2ch
static const dmx_channel_t PERS_4_CH[] = {
    { CH_PIXEL_MIRROR, ID_STRIP_MIRROR,  VEYRON_SEC_STRIP,  "Strip Mirror",  0, 0, (VEYRON_NUM_PIXELS_1 / 2) * 3 },
    { CH_ACCENT_RGB,   ID_ACCENT_PIXELS, VEYRON_SEC_ACCENT, "Accent Pixels", 0, 0, VEYRON_NUM_PIXELS_2 * 3 },
    { CH_STROBE,       ID_STROBE_STRIP,  VEYRON_SEC_STROBE, "Strobe Strip",  0, 1, 1 },
    { CH_STROBE,       ID_STROBE_ACCENT, VEYRON_SEC_STROBE, "Strobe Accent", 0, 1, 1 },
};

// ── P5: Grouped 2px + Strobe  (68 ch) ────────────────────────────────────────
// strip: 20×3 = 60ch grouped (1 ch → 2 pixels) | accent: 2×3 = 6ch | strobe: 2ch
static const dmx_channel_t PERS_5_CH[] = {
    { CH_PIXEL_GROUP2, ID_STRIP_GROUP2,  VEYRON_SEC_STRIP,  "Strip Group2",  0, 0, (VEYRON_NUM_PIXELS_1 / 2) * 3 },
    { CH_ACCENT_RGB,   ID_ACCENT_PIXELS, VEYRON_SEC_ACCENT, "Accent Pixels", 0, 0, VEYRON_NUM_PIXELS_2 * 3 },
    { CH_STROBE,       ID_STROBE_STRIP,  VEYRON_SEC_STROBE, "Strobe Strip",  0, 1, 1 },
    { CH_STROBE,       ID_STROBE_ACCENT, VEYRON_SEC_STROBE, "Strobe Accent", 0, 1, 1 },
};

// ── Master personality table ──────────────────────────────────────────────────
static const personality_t VEYRON_PERSONALITIES[] = {
    { "Full Pixel + Strobe",   128, PERS_1_CH, 4 },
    { "Full Pixel",            126, PERS_2_CH, 2 },
    { "Broadcast RGB + Strobe",  6, PERS_3_CH, 4 },
    { "Mirror + Strobe",        68, PERS_4_CH, 4 },
    { "Grouped 2px + Strobe",   68, PERS_5_CH, 4 },
};

#define VEYRON_NUM_PERSONALITIES  (sizeof(VEYRON_PERSONALITIES) / sizeof(VEYRON_PERSONALITIES[0]))

#endif // RAVLIGHT_FIXTURE_VEYRON
