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

// DMX personality (owned by this fixture, not by DmxConfig)
typedef enum : uint8_t {
    PERSONALITY_1 = 1,
    PERSONALITY_2,
    PERSONALITY_3,
    PERSONALITY_4,
    PERSONALITY_5
} FixturePersonality;

// Fixture runtime config — persisted via fixtureConfigSerialize/Deserialize
struct VeyronConfig {
    FixturePersonality personality;
    uint16_t           rgbwStart;
    uint16_t           whiteStart;
    uint16_t           strobeStart;
};

extern VeyronConfig veyronConfig;

#endif // RAVLIGHT_FIXTURE_VEYRON
