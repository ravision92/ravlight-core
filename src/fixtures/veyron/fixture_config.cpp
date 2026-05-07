#ifdef RAVLIGHT_FIXTURE_VEYRON
#include "fixture_config.h"
#include "fixtures/veyron/fixture.h"
#include "config.h"

VeyronConfig veyronConfig;

void fixtureConfigDefaults() {
    veyronConfig.personality = PERSONALITY_1;
    veyronConfig.rgbwStart   = 1;
    veyronConfig.whiteStart  = 121;
    veyronConfig.strobeStart = 127;
    veyronConfig.DimCurves   = LINEAR;
}

void fixtureConfigSerialize(JsonObject& fix) {
    fix["personality"] = (int)veyronConfig.personality;
    fix["rgbw"]        = veyronConfig.rgbwStart;
    fix["white"]       = veyronConfig.whiteStart;
    fix["strobe"]      = veyronConfig.strobeStart;
    fix["dimCurve"]    = veyronConfig.DimCurves;
}

void fixtureConfigDeserialize(const JsonObject& fix) {
    veyronConfig.personality = static_cast<FixturePersonality>(fix["personality"] | (int)PERSONALITY_1);
    veyronConfig.rgbwStart   = fix["rgbw"]      | (uint16_t)1;
    veyronConfig.whiteStart  = fix["white"]     | (uint16_t)121;
    veyronConfig.strobeStart = fix["strobe"]    | (uint16_t)127;
    veyronConfig.DimCurves   = fix["dimCurve"]  | (uint16_t)LINEAR;
}

void fixtureGetDmxMap(JsonObject& map) {
    char uKey[8];
    snprintf(uKey, sizeof(uKey), "%u", dmxConfig.startUniverse);
    JsonArray arr = map.createNestedArray(uKey);
    JsonArray r1 = arr.createNestedArray();
    r1.add(veyronConfig.rgbwStart);
    r1.add(veyronConfig.rgbwStart + VEYRON_NUM_PIXELS_1 * 3 - 1);
    JsonArray r2 = arr.createNestedArray();
    r2.add(veyronConfig.whiteStart);
    r2.add(veyronConfig.whiteStart + VEYRON_NUM_PIXELS_2 * 4 - 1);
    JsonArray r3 = arr.createNestedArray();
    r3.add(veyronConfig.strobeStart);
    r3.add(veyronConfig.strobeStart);
}

#endif // RAVLIGHT_FIXTURE_VEYRON
