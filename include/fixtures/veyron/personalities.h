#pragma once
#ifdef RAVLIGHT_FIXTURE_VEYRON
#include <stdint.h>
#include "core/dmx_patch.h"
#include "fixtures/veyron/fixture_ids.h"
#include "fixtures/veyron/fixture.h"

// Veyron section indices for patch_state_t.section_start[]
#define VEYRON_SEC_STRIP   0   // base = RGBWstartAddress
#define VEYRON_SEC_ACCENT  1   // base = WhStartAddress
#define VEYRON_SEC_STROBE  2   // base = functionStartAddress (shutter/dimmer/macro block)

// Strobe/Shutter power curve: interval_ms = 24.454 * rate^0.423, mapped to [MIN..MAX]
#define VEYRON_STROBE_CURVE_A   24.454f
#define VEYRON_STROBE_CURVE_B   0.423f
#define VEYRON_STROBE_RATE_MIN  2500    // ms — slowest strobe
#define VEYRON_STROBE_RATE_MAX  40      // ms — fastest strobe

// ── P1: Full Pixel (Legacy)  (128 ch) ───────────────────────────────────────
// Frozen simple layout — RGB pixels + 6 accent whites + shutter strip/accent,
// no Master Dimmer/Macro. Kept stable on purpose so already-patched consoles
// aren't broken by the newer Rich tier (P2); the Shutter channels still use
// the current zone decode (only the layout/footprint is "legacy", not the
// per-byte behavior).
static const dmx_channel_t PERS_1_CH[] = {
    { CH_PIXEL_RGB,    ID_STRIP_PIXELS,  VEYRON_SEC_STRIP,  "Strip Pixels",  0, 0, VEYRON_NUM_PIXELS_1 * 3 },
    { CH_WHITE,        ID_ACCENT_WHITE_1, VEYRON_SEC_ACCENT, "Accent White 1", 0, 0, 1 },
    { CH_WHITE,        ID_ACCENT_WHITE_2, VEYRON_SEC_ACCENT, "Accent White 2", 0, 0, 1 },
    { CH_WHITE,        ID_ACCENT_WHITE_3, VEYRON_SEC_ACCENT, "Accent White 3", 0, 0, 1 },
    { CH_WHITE,        ID_ACCENT_WHITE_4, VEYRON_SEC_ACCENT, "Accent White 4", 0, 0, 1 },
    { CH_WHITE,        ID_ACCENT_WHITE_5, VEYRON_SEC_ACCENT, "Accent White 5", 0, 0, 1 },
    { CH_WHITE,        ID_ACCENT_WHITE_6, VEYRON_SEC_ACCENT, "Accent White 6", 0, 0, 1 },
    { CH_STROBE,       ID_STROBE_STRIP,  VEYRON_SEC_STROBE, "Shutter Strip",  0, 1, 1 },
    { CH_STROBE,       ID_STROBE_ACCENT, VEYRON_SEC_STROBE, "Shutter Accent", 0, 1, 1 },
};

// ── P2: Full Pixel + Macro  (133 ch) ────────────────────────────────────────
// strip: 40×3 = 120ch | accent: 6ch | shutter: 2ch | master dimmer: 2ch |
// Pixel Macro + White Macro + Speed: 3ch. Pixel Macro preserves each of the
// 40 physical pixels' own patched color (Chase/Twinkle/Fade/Slide/Mirror
// multiply a brightness mask onto it); Rainbow/Fire override it entirely.
static const dmx_channel_t PERS_2_CH[] = {
    { CH_PIXEL_RGB,    ID_STRIP_PIXELS,  VEYRON_SEC_STRIP,  "Strip Pixels",  0, 0, VEYRON_NUM_PIXELS_1 * 3 },
    { CH_WHITE,        ID_ACCENT_WHITE_1, VEYRON_SEC_ACCENT, "Accent White 1", 0, 0, 1 },
    { CH_WHITE,        ID_ACCENT_WHITE_2, VEYRON_SEC_ACCENT, "Accent White 2", 0, 0, 1 },
    { CH_WHITE,        ID_ACCENT_WHITE_3, VEYRON_SEC_ACCENT, "Accent White 3", 0, 0, 1 },
    { CH_WHITE,        ID_ACCENT_WHITE_4, VEYRON_SEC_ACCENT, "Accent White 4", 0, 0, 1 },
    { CH_WHITE,        ID_ACCENT_WHITE_5, VEYRON_SEC_ACCENT, "Accent White 5", 0, 0, 1 },
    { CH_WHITE,        ID_ACCENT_WHITE_6, VEYRON_SEC_ACCENT, "Accent White 6", 0, 0, 1 },
    { CH_STROBE,       ID_STROBE_STRIP,  VEYRON_SEC_STROBE, "Shutter Strip",  0, 1, 1 },
    { CH_STROBE,       ID_STROBE_ACCENT, VEYRON_SEC_STROBE, "Shutter Accent", 0, 1, 1 },
    { CH_INTENSITY,    ID_DIMMER_STRIP,  VEYRON_SEC_STROBE, "Master Dimmer Strip",  255, 0, 1 },
    { CH_INTENSITY,    ID_DIMMER_ACCENT, VEYRON_SEC_STROBE, "Master Dimmer Accent", 255, 0, 1 },
    { CH_EFFECT,       ID_PIXEL_MACRO,   VEYRON_SEC_STROBE, "Pixel Macro",    0, 1, 1 },
    { CH_EFFECT,       ID_WHITE_MACRO,   VEYRON_SEC_STROBE, "White Macro",    0, 1, 1 },
    { CH_SPEED,        ID_MACRO_SPEED,   VEYRON_SEC_STROBE, "Macro Speed",    0, 0, 1 },
};

// ── P3: Full Pixel  (126 ch) ────────────────────────────────────────────────
// Bare tier — strip: 40×3 = 120ch | accent: 6ch, no shutter/dimmer/macro.
static const dmx_channel_t PERS_3_CH[] = {
    { CH_PIXEL_RGB,    ID_STRIP_PIXELS,  VEYRON_SEC_STRIP,  "Strip Pixels",  0, 0, VEYRON_NUM_PIXELS_1 * 3 },
    { CH_WHITE,        ID_ACCENT_WHITE_1, VEYRON_SEC_ACCENT, "Accent White 1", 0, 0, 1 },
    { CH_WHITE,        ID_ACCENT_WHITE_2, VEYRON_SEC_ACCENT, "Accent White 2", 0, 0, 1 },
    { CH_WHITE,        ID_ACCENT_WHITE_3, VEYRON_SEC_ACCENT, "Accent White 3", 0, 0, 1 },
    { CH_WHITE,        ID_ACCENT_WHITE_4, VEYRON_SEC_ACCENT, "Accent White 4", 0, 0, 1 },
    { CH_WHITE,        ID_ACCENT_WHITE_5, VEYRON_SEC_ACCENT, "Accent White 5", 0, 0, 1 },
    { CH_WHITE,        ID_ACCENT_WHITE_6, VEYRON_SEC_ACCENT, "Accent White 6", 0, 0, 1 },
};

// ── P4: Mirror + Macro  (73 ch) ─────────────────────────────────────────────
// strip: 20×3 = 60ch mirrored to 40px | accent: 6ch | shutter: 2ch |
// master dimmer: 2ch | Zone Macro + White Macro + Speed: 3ch.
static const dmx_channel_t PERS_4_CH[] = {
    { CH_PIXEL_MIRROR, ID_STRIP_MIRROR,  VEYRON_SEC_STRIP,  "Strip Mirror",  0, 0, (VEYRON_NUM_PIXELS_1 / 2) * 3 },
    { CH_WHITE,        ID_ACCENT_WHITE_1, VEYRON_SEC_ACCENT, "Accent White 1", 0, 0, 1 },
    { CH_WHITE,        ID_ACCENT_WHITE_2, VEYRON_SEC_ACCENT, "Accent White 2", 0, 0, 1 },
    { CH_WHITE,        ID_ACCENT_WHITE_3, VEYRON_SEC_ACCENT, "Accent White 3", 0, 0, 1 },
    { CH_WHITE,        ID_ACCENT_WHITE_4, VEYRON_SEC_ACCENT, "Accent White 4", 0, 0, 1 },
    { CH_WHITE,        ID_ACCENT_WHITE_5, VEYRON_SEC_ACCENT, "Accent White 5", 0, 0, 1 },
    { CH_WHITE,        ID_ACCENT_WHITE_6, VEYRON_SEC_ACCENT, "Accent White 6", 0, 0, 1 },
    { CH_STROBE,       ID_STROBE_STRIP,  VEYRON_SEC_STROBE, "Shutter Strip",  0, 1, 1 },
    { CH_STROBE,       ID_STROBE_ACCENT, VEYRON_SEC_STROBE, "Shutter Accent", 0, 1, 1 },
    { CH_INTENSITY,    ID_DIMMER_STRIP,  VEYRON_SEC_STROBE, "Master Dimmer Strip",  255, 0, 1 },
    { CH_INTENSITY,    ID_DIMMER_ACCENT, VEYRON_SEC_STROBE, "Master Dimmer Accent", 255, 0, 1 },
    { CH_EFFECT,       ID_ZONE_MACRO,    VEYRON_SEC_STROBE, "Zone Macro",     0, 1, 1 },
    { CH_EFFECT,       ID_WHITE_MACRO,   VEYRON_SEC_STROBE, "White Macro",    0, 1, 1 },
    { CH_SPEED,        ID_MACRO_SPEED,   VEYRON_SEC_STROBE, "Macro Speed",    0, 0, 1 },
};

// ── P5: Mirror  (66 ch) ──────────────────────────────────────────────────────
// Bare tier — strip: 20×3 = 60ch mirrored to 40px | accent: 6ch, no
// shutter/dimmer/macro.
static const dmx_channel_t PERS_5_CH[] = {
    { CH_PIXEL_MIRROR, ID_STRIP_MIRROR,  VEYRON_SEC_STRIP,  "Strip Mirror",  0, 0, (VEYRON_NUM_PIXELS_1 / 2) * 3 },
    { CH_WHITE,        ID_ACCENT_WHITE_1, VEYRON_SEC_ACCENT, "Accent White 1", 0, 0, 1 },
    { CH_WHITE,        ID_ACCENT_WHITE_2, VEYRON_SEC_ACCENT, "Accent White 2", 0, 0, 1 },
    { CH_WHITE,        ID_ACCENT_WHITE_3, VEYRON_SEC_ACCENT, "Accent White 3", 0, 0, 1 },
    { CH_WHITE,        ID_ACCENT_WHITE_4, VEYRON_SEC_ACCENT, "Accent White 4", 0, 0, 1 },
    { CH_WHITE,        ID_ACCENT_WHITE_5, VEYRON_SEC_ACCENT, "Accent White 5", 0, 0, 1 },
    { CH_WHITE,        ID_ACCENT_WHITE_6, VEYRON_SEC_ACCENT, "Accent White 6", 0, 0, 1 },
};

// ── P6: Grouped 2px + Macro  (73 ch) ────────────────────────────────────────
// strip: 20×3 = 60ch grouped (1 ch → 2 pixels) | accent: 6ch | shutter: 2ch |
// master dimmer: 2ch | Zone Macro + White Macro + Speed: 3ch.
static const dmx_channel_t PERS_6_CH[] = {
    { CH_PIXEL_GROUP2, ID_STRIP_GROUP2,  VEYRON_SEC_STRIP,  "Strip Group2",  0, 0, (VEYRON_NUM_PIXELS_1 / 2) * 3 },
    { CH_WHITE,        ID_ACCENT_WHITE_1, VEYRON_SEC_ACCENT, "Accent White 1", 0, 0, 1 },
    { CH_WHITE,        ID_ACCENT_WHITE_2, VEYRON_SEC_ACCENT, "Accent White 2", 0, 0, 1 },
    { CH_WHITE,        ID_ACCENT_WHITE_3, VEYRON_SEC_ACCENT, "Accent White 3", 0, 0, 1 },
    { CH_WHITE,        ID_ACCENT_WHITE_4, VEYRON_SEC_ACCENT, "Accent White 4", 0, 0, 1 },
    { CH_WHITE,        ID_ACCENT_WHITE_5, VEYRON_SEC_ACCENT, "Accent White 5", 0, 0, 1 },
    { CH_WHITE,        ID_ACCENT_WHITE_6, VEYRON_SEC_ACCENT, "Accent White 6", 0, 0, 1 },
    { CH_STROBE,       ID_STROBE_STRIP,  VEYRON_SEC_STROBE, "Shutter Strip",  0, 1, 1 },
    { CH_STROBE,       ID_STROBE_ACCENT, VEYRON_SEC_STROBE, "Shutter Accent", 0, 1, 1 },
    { CH_INTENSITY,    ID_DIMMER_STRIP,  VEYRON_SEC_STROBE, "Master Dimmer Strip",  255, 0, 1 },
    { CH_INTENSITY,    ID_DIMMER_ACCENT, VEYRON_SEC_STROBE, "Master Dimmer Accent", 255, 0, 1 },
    { CH_EFFECT,       ID_ZONE_MACRO,    VEYRON_SEC_STROBE, "Zone Macro",     0, 1, 1 },
    { CH_EFFECT,       ID_WHITE_MACRO,   VEYRON_SEC_STROBE, "White Macro",    0, 1, 1 },
    { CH_SPEED,        ID_MACRO_SPEED,   VEYRON_SEC_STROBE, "Macro Speed",    0, 0, 1 },
};

// ── P7: Grouped 2px  (66 ch) ─────────────────────────────────────────────────
// Bare tier — strip: 20×3 = 60ch grouped (1 ch → 2 pixels) | accent: 6ch, no
// shutter/dimmer/macro.
static const dmx_channel_t PERS_7_CH[] = {
    { CH_PIXEL_GROUP2, ID_STRIP_GROUP2,  VEYRON_SEC_STRIP,  "Strip Group2",  0, 0, (VEYRON_NUM_PIXELS_1 / 2) * 3 },
    { CH_WHITE,        ID_ACCENT_WHITE_1, VEYRON_SEC_ACCENT, "Accent White 1", 0, 0, 1 },
    { CH_WHITE,        ID_ACCENT_WHITE_2, VEYRON_SEC_ACCENT, "Accent White 2", 0, 0, 1 },
    { CH_WHITE,        ID_ACCENT_WHITE_3, VEYRON_SEC_ACCENT, "Accent White 3", 0, 0, 1 },
    { CH_WHITE,        ID_ACCENT_WHITE_4, VEYRON_SEC_ACCENT, "Accent White 4", 0, 0, 1 },
    { CH_WHITE,        ID_ACCENT_WHITE_5, VEYRON_SEC_ACCENT, "Accent White 5", 0, 0, 1 },
    { CH_WHITE,        ID_ACCENT_WHITE_6, VEYRON_SEC_ACCENT, "Accent White 6", 0, 0, 1 },
};

// ── P8: RGBW + Macro  (10 ch) ────────────────────────────────────────────────
// strip: 3ch cast | accent: 1ch white | shutter: 2ch | master intensity: 1ch
// (single, scales strip+accent together) | RGB Macro + White Macro + Speed: 3ch
// Macro idle -> manual broadcast (Strip Color/Accent White); macro engaged ->
// built-in animated pattern using Strip Color as its base color (self-colored
// macros like Rainbow/Fire ignore it) — reuses ID_STRIP_CAST instead of a
// dedicated macro-color block.
static const dmx_channel_t PERS_8_CH[] = {
    { CH_PIXEL_CAST,   ID_STRIP_CAST,        VEYRON_SEC_STRIP,  "Strip Color",       0, 0, 3 },
    { CH_ACCENT_WHITE, ID_ACCENT_WHITE,      VEYRON_SEC_ACCENT, "Accent White",      0, 0, 1 },
    { CH_STROBE,       ID_STROBE_STRIP,      VEYRON_SEC_STROBE, "Shutter Strip",     0, 1, 1 },
    { CH_STROBE,       ID_STROBE_ACCENT,     VEYRON_SEC_STROBE, "Shutter Accent",    0, 1, 1 },
    { CH_INTENSITY,    ID_MASTER_INTENSITY,  VEYRON_SEC_STROBE, "Master Intensity",  255, 0, 1 },
    { CH_EFFECT,       ID_RGB_MACRO,         VEYRON_SEC_STROBE, "RGB Macro",         0, 1, 1 },
    { CH_EFFECT,       ID_WHITE_MACRO,       VEYRON_SEC_STROBE, "White Macro",       0, 1, 1 },
    { CH_SPEED,        ID_MACRO_SPEED,       VEYRON_SEC_STROBE, "Macro Speed",       0, 0, 1 },
};

// ── P9: RGBW  (4 ch) ─────────────────────────────────────────────────────────
// Bare tier — strip: 3ch cast | accent: 1ch white, no shutter/dimmer/macro.
static const dmx_channel_t PERS_9_CH[] = {
    { CH_PIXEL_CAST,   ID_STRIP_CAST,    VEYRON_SEC_STRIP,  "Strip Color",   0, 0, 3 },
    { CH_ACCENT_WHITE, ID_ACCENT_WHITE,  VEYRON_SEC_ACCENT, "Accent White",  0, 0, 1 },
};

// ── Master personality table ──────────────────────────────────────────────────
static const personality_t VEYRON_PERSONALITIES[] = {
    { "Full Pixel (Legacy)",   128, PERS_1_CH, 9 },
    { "Full Pixel + Macro",    133, PERS_2_CH, 13 },
    { "Full Pixel",            126, PERS_3_CH, 7 },
    { "Mirror + Macro",         73, PERS_4_CH, 13 },
    { "Mirror",                 66, PERS_5_CH, 7 },
    { "Grouped 2px + Macro",    73, PERS_6_CH, 13 },
    { "Grouped 2px",            66, PERS_7_CH, 7 },
    { "RGBW + Macro",           10, PERS_8_CH, 8 },
    { "RGBW",                    4, PERS_9_CH, 2 },
};

#define VEYRON_NUM_PERSONALITIES  (sizeof(VEYRON_PERSONALITIES) / sizeof(VEYRON_PERSONALITIES[0]))

#endif // RAVLIGHT_FIXTURE_VEYRON
