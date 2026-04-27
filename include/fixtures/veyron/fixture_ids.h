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
    ID_ACCENT_PIXELS  = 0x10,   // individual RGB accent pixels
    ID_ACCENT_WHITE   = 0x11,   // broadcast white to all accent pixels
    ID_STROBE_ACCENT  = 0x12,   // strobe for accent
    ID_DIMMER_ACCENT  = 0x13,   // master dimmer accent (reserved)

    // Global controls (section 2 for strobe channels)
    ID_DIM_CURVE      = 0x20,   // dimming curve select (snap, reserved)
    ID_HIGHLIGHT      = 0x21,   // highlight trigger (snap >127=on, reserved)
} veyron_ch_id_t;
