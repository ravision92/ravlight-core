#pragma once
#include <stdint.h>

// Veyron fixture — unique channel IDs
// Do NOT renumber after first commit: values are persisted in saved configs.
typedef enum : uint8_t {
    // Strip WS2811 (section 0)
    ID_STRIP_PIXELS   = 0x01,   // individual RGB pixels
    ID_STRIP_CAST     = 0x02,   // broadcast one color to all pixels
    ID_STRIP_MIRROR   = 0x03,   // 20ch mirrored to 40px
    ID_STRIP_GROUP2   = 0x04,   // 20ch grouped 2px per channel
    ID_STROBE_STRIP   = 0x05,   // strobe for strip
    ID_DIMMER_STRIP   = 0x06,   // master dimmer strip (reserved)

    // Accent P9813 (section 1)
    ID_ACCENT_PIXELS  = 0x10,   // deprecated: was "2 RGB accent pixels" — the P9813
                                // pair physically drives 6 independent white COB LEDs,
                                // not 2 RGB pixels. Kept defined (never renumber past
                                // values), superseded by ID_ACCENT_WHITE_1..6 below.
    ID_ACCENT_WHITE   = 0x11,   // broadcast white to all accent pixels
    ID_STROBE_ACCENT  = 0x12,   // strobe for accent
    ID_DIMMER_ACCENT  = 0x13,   // master dimmer accent (reserved)
    ID_ACCENT_WHITE_1 = 0x14,   // individual accent white LED 1..6
    ID_ACCENT_WHITE_2 = 0x15,
    ID_ACCENT_WHITE_3 = 0x16,
    ID_ACCENT_WHITE_4 = 0x17,
    ID_ACCENT_WHITE_5 = 0x18,
    ID_ACCENT_WHITE_6 = 0x19,

    // Global controls (section 2 for strobe channels)
    ID_DIM_CURVE      = 0x20,   // dimming curve select (snap, reserved)
    ID_HIGHLIGHT      = 0x21,   // highlight trigger (snap >127=on, reserved)
    ID_MASTER_INTENSITY = 0x22, // single combined dimmer (RGBW+Strobe+Macro
                                // tier only) — scales strip+accent together,
                                // distinct from the split ID_DIMMER_STRIP/
                                // ID_DIMMER_ACCENT used elsewhere.

    // Macro-select channels bridging to the built-in effects engine
    // (src/effects.cpp) instead of expecting per-pixel data from the console.
    ID_RGB_MACRO       = 0x30,   // range-select: which built-in strip effect
    ID_RGB_MACRO_COLOR = 0x31,   // deprecated: was a dedicated 3ch macro-color
                                 // block for the old Auto Program personality.
                                 // Superseded — the RGBW+Macro tier reuses
                                 // ID_STRIP_CAST as the macro's base color
                                 // instead. Kept defined (never renumber).
    ID_WHITE_MACRO     = 0x32,   // range-select: which built-in accent effect
    ID_MACRO_SPEED     = 0x33,   // shared animation speed for RGB/Zone/White macros
    ID_ZONE_MACRO      = 0x34,   // range-select: zone-color-preserving movement
                                 // mask for Mirror/Grouped Macro personalities
                                 // (distinct decode/behavior from ID_RGB_MACRO —
                                 // multiplies each zone's own patched color by a
                                 // brightness mask instead of replacing it)
    ID_PIXEL_MACRO     = 0x35,   // same decode/engine as ID_ZONE_MACRO (shared
                                 // ZoneMacro enum/decodeZoneMacro()), but over
                                 // all 40 physical pixels directly (Full Pixel
                                 // Rich tier — no mirror/group mapping needed)
} veyron_ch_id_t;
