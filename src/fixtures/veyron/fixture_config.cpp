#ifdef RAVLIGHT_FIXTURE_VEYRON
#include "fixture_config.h"
#include "fixtures/veyron/fixture.h"

VeyronConfig veyronConfig;

void fixtureConfigDefaults() {
    veyronConfig.personality = PERSONALITY_1;
    veyronConfig.rgbwStart   = 1;
    veyronConfig.whiteStart  = 121;
    veyronConfig.strobeStart = 127;
}

void fixtureConfigSerialize(JsonObject& fix) {
    fix["personality"] = (int)veyronConfig.personality;
    fix["rgbw"]        = veyronConfig.rgbwStart;
    fix["white"]       = veyronConfig.whiteStart;
    fix["strobe"]      = veyronConfig.strobeStart;
}

void fixtureConfigDeserialize(const JsonObject& fix) {
    veyronConfig.personality = static_cast<FixturePersonality>(fix["personality"] | (int)PERSONALITY_1);
    veyronConfig.rgbwStart   = fix["rgbw"]   | (uint16_t)1;
    veyronConfig.whiteStart  = fix["white"]  | (uint16_t)121;
    veyronConfig.strobeStart = fix["strobe"] | (uint16_t)127;
}

#endif // RAVLIGHT_FIXTURE_VEYRON
