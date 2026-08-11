#pragma once
#ifdef RAVLIGHT_FIXTURE_VEYRON
#include <stdint.h>

// Identity
#define VEYRON_FIXTURE_NAME    "Veyron"
#define PROJECT_NAME           "Veyron"
#define FIXTURE_STATUS         "stable"

// Compile-time constants
#define VEYRON_NUM_PIXELS_1        40    // main WS2811 strip pixel count
#define VEYRON_NUM_PIXELS_2         2    // P9813 accent pixel count (drives 6 COB LEDs)
#define VEYRON_STROBE_DURATION     20    // strobe ON time in milliseconds
#define VEYRON_STEP_HIGHLIGHT     100    // highlight animation step interval in milliseconds
#define VEYRON_HIGHLIGHT_DURATION 6000   // total highlight sequence duration in milliseconds

// Dimming curve — values match HTML form options (1-4)
typedef enum {
    LINEAR = 1,
    SQUARE,
    INVERSE_SQUARE,
    S_CURVE
} DimmingCurve;

// DMX personality (owned by this fixture, not by DmxConfig)
typedef enum : uint8_t {
    PERSONALITY_1 = 1,   // Full Pixel (Legacy) — frozen simple layout, no dimmer/macro
    PERSONALITY_2,       // Full Pixel + Macro — Shutter + Master Dimmer + Pixel Macro
    PERSONALITY_3,       // Full Pixel — bare pixel-only tier
    PERSONALITY_4,       // Mirror + Macro — Shutter + Master Dimmer + Zone Macro
    PERSONALITY_5,       // Mirror — bare pixel-only tier
    PERSONALITY_6,       // Grouped 2px + Macro — Shutter + Master Dimmer + Zone Macro
    PERSONALITY_7,       // Grouped 2px — bare pixel-only tier
    PERSONALITY_8,       // RGBW + Macro — Shutter + Master Intensity + RGB Macro
    PERSONALITY_9        // RGBW — bare tier
} FixturePersonality;

// Fixture runtime config — persisted via fixtureConfigSerialize/Deserialize
struct VeyronConfig {
    FixturePersonality personality;
    uint16_t           rgbwStart;
    uint16_t           whiteStart;
    uint16_t           functionStart;   // base address of the "function" section:
                                         // shutter/dimmer/macro/speed channels — no
                                         // longer just strobe now that this block
                                         // hosts Master Dimmer/Intensity and the
                                         // macro engines too (renamed from strobeStart)
    uint16_t           DimCurves;
    bool               statusLedEnable;   // reflect net/OTA status on the strip (see dmx_fixture.cpp)
};

extern VeyronConfig veyronConfig;

#endif // RAVLIGHT_FIXTURE_VEYRON
