#ifdef RAVLIGHT_FIXTURE_VEYRON
#include "fixture_config.h"
#include "fixtures/veyron/fixture.h"
#include "config.h"
#ifdef RAVLIGHT_MODULE_EFFECTS
#include "effects.h"
#endif

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

// Veyron's renderer reads every parameter from veyronConfig on each
// handleDMX() iteration — personality (which dispatches to the right
// handler), the address fields (used through getChannelById), and the
// dim curves (apply_dimming reads them per pixel). No driver state to
// re-init: the next frame after a save already sees the new values.
// Always returning false here makes every fixture-level save apply
// live, without a restart.
bool fixtureApplyLive() { return false; }

// Effects engine targets — Veyron has two pixel ranges and a strobe
// byte. Strobe is excluded (it's a function channel, not a pixel — the
// engine would treat its byte as an R component of a phantom pixel and
// trigger the strobe randomly). For now only Personality 1 is honoured;
// other personalities return 0 targets, falling back to "no effects on
// this fixture" until their pixel layouts are catalogued explicitly.
uint8_t fixtureGetEffectTargets(fx_target_t* out, uint8_t max) {
    if (veyronConfig.personality != PERSONALITY_1) return 0;
    uint8_t n = 0;
    if (n < max) {
        out[n].universe     = dmxConfig.startUniverse;
        out[n].dmx_start    = veyronConfig.rgbwStart;   // 40 px main strip
        out[n].pixel_count  = VEYRON_NUM_PIXELS_1;
        out[n].ch_per_pixel = 3;
        n++;
    }
    if (n < max) {
        out[n].universe     = dmxConfig.startUniverse;
        out[n].dmx_start    = veyronConfig.whiteStart;  // 2 px P9813 accent
        out[n].pixel_count  = VEYRON_NUM_PIXELS_2;
        out[n].ch_per_pixel = 3;
        n++;
    }
    return n;
}

// Function-channel overlay invoked by the effects engine after pixels
// are rendered. Writes:
//   ch whiteStart..whiteStart+5   ← effectsConfig.white       (6× COB white)
//   ch strobeStart                ← effectsConfig.strobeRgb   (strip strobe)
//   ch strobeStart+1              ← effectsConfig.strobeWhite (accent strobe)
// Only acts on this fixture's universe; ignored for any other universe
// the effects engine happens to be emitting.
void fixtureApplyEffectFunctions(uint8_t* buf, uint16_t universe) {
#ifdef RAVLIGHT_MODULE_EFFECTS
    if (universe != dmxConfig.startUniverse) return;
    if (veyronConfig.personality != PERSONALITY_1) return;
    // 6 white accent channels (the P9813 pair physically drives 6 COB
    // white LEDs — treated as individual intensities, not RGB pixels).
    uint16_t ws = veyronConfig.whiteStart;
    for (int i = 0; i < 6 && ws + i <= 512; i++) buf[ws + i] = effectsConfig.white;
    // Two strobe rate channels at the personality's strobe block.
    uint16_t ss = veyronConfig.strobeStart;
    if (ss     >= 1 && ss     <= 512) buf[ss    ] = effectsConfig.strobeRgb;
    if (ss + 1 >= 1 && ss + 1 <= 512) buf[ss + 1] = effectsConfig.strobeWhite;
#else
    (void)buf; (void)universe;
#endif
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
