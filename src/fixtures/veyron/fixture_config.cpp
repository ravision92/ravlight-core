#ifdef RAVLIGHT_FIXTURE_VEYRON
#include "fixture_config.h"
#include "fixtures/veyron/fixture.h"
#include "fixtures/veyron/personalities.h"
#include "fixtures/veyron/dmx_fixture.h"
#include "config.h"
#ifdef RAVLIGHT_MODULE_EFFECTS
#include "effects.h"
#endif

VeyronConfig veyronConfig;

void fixtureConfigDefaults() {
    veyronConfig.personality    = PERSONALITY_1;
    veyronConfig.rgbwStart      = 1;
    veyronConfig.whiteStart     = 121;
    veyronConfig.functionStart  = 127;
    veyronConfig.DimCurves      = LINEAR;
    veyronConfig.statusLedEnable = true;
}

void fixtureConfigSerialize(JsonObject& fix) {
    fix["personality"] = (int)veyronConfig.personality;
    fix["rgbw"]        = veyronConfig.rgbwStart;
    fix["white"]       = veyronConfig.whiteStart;
    fix["function"]    = veyronConfig.functionStart;
    fix["dimCurve"]    = veyronConfig.DimCurves;
    fix["statusLed"]   = veyronConfig.statusLedEnable;
}

void fixtureConfigDeserialize(const JsonObject& fix) {
    veyronConfig.personality   = static_cast<FixturePersonality>(fix["personality"] | (int)PERSONALITY_1);
    veyronConfig.rgbwStart     = fix["rgbw"]      | (uint16_t)1;
    veyronConfig.whiteStart    = fix["white"]     | (uint16_t)121;
    veyronConfig.functionStart = fix["function"]  | (uint16_t)127;
    veyronConfig.DimCurves     = fix["dimCurve"]  | (uint16_t)LINEAR;
    veyronConfig.statusLedEnable = fix["statusLed"] | true;
}

// The DMX personality dispatch in handleDMX() reads veyronConfig.personality
// directly, but the actual channel offsets (getChannelById/getChannelBlockById)
// read from veyron_patch — a runtime struct private to dmx_fixture.cpp that
// fixtureConfigDeserialize() has no way to touch. applyVeyronConfigLive()
// pushes the freshly deserialized values into it. No driver state to
// re-init otherwise, so this always applies live, without a restart.
bool fixtureApplyLive() {
    applyVeyronConfigLive();
    return false;
}

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
    // No accent entry here: the P9813 pair's 6 bytes are independent white
    // COB intensities, not an RGB pixel pair — painting rainbow/chase RGB
    // into them wouldn't mean anything. fixtureApplyEffectFunctions() below
    // already drives all 6 with a single white scalar, which is the only
    // sensible effects-engine treatment for a non-color output.
    return n;
}

// Function-channel overlay invoked by the effects engine after pixels
// are rendered. Writes:
//   ch whiteStart..whiteStart+5   ← effectsConfig.white       (6× COB white)
//   ch functionStart               ← effectsConfig.strobeRgb   (strip strobe)
//   ch functionStart+1             ← effectsConfig.strobeWhite (accent strobe)
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
    // Two strobe rate channels at the start of the personality's function block.
    uint16_t fs = veyronConfig.functionStart;
    if (fs     >= 1 && fs     <= 512) buf[fs    ] = effectsConfig.strobeRgb;
    if (fs + 1 >= 1 && fs + 1 <= 512) buf[fs + 1] = effectsConfig.strobeWhite;
#else
    (void)buf; (void)universe;
#endif
}

// DMX Monitor highlight ranges — computed from the CURRENT personality's own
// channel-table widths (same per-section sum as dmx_fixture.cpp's
// relayoutSectionAddresses()) instead of hardcoded Personality-1-shaped
// formulas, so the highlighted gap actually matches whatever footprint the
// selected personality has (RGBW's 3ch strip vs Full Pixel's 120ch, etc.).
void fixtureGetDmxMap(JsonObject& map) {
    char uKey[8];
    snprintf(uKey, sizeof(uKey), "%u", dmxConfig.startUniverse);
    JsonArray arr = map.createNestedArray(uKey);

    const personality_t& pers = VEYRON_PERSONALITIES[(uint8_t)veyronConfig.personality - 1];
    uint16_t stripWidth = 0, accentWidth = 0, functionWidth = 0;
    for (uint8_t i = 0; i < pers.n_channels; i++) {
        const dmx_channel_t& ch = pers.channels[i];
        if (ch.section == VEYRON_SEC_STRIP)  stripWidth    += ch.count;
        if (ch.section == VEYRON_SEC_ACCENT) accentWidth   += ch.count;
        if (ch.section == VEYRON_SEC_STROBE) functionWidth += ch.count;
    }

    if (stripWidth) {
        JsonArray r1 = arr.createNestedArray();
        r1.add(veyronConfig.rgbwStart);
        r1.add(veyronConfig.rgbwStart + stripWidth - 1);
    }
    if (accentWidth) {
        JsonArray r2 = arr.createNestedArray();
        r2.add(veyronConfig.whiteStart);
        r2.add(veyronConfig.whiteStart + accentWidth - 1);
    }
    if (functionWidth) {
        JsonArray r3 = arr.createNestedArray();
        r3.add(veyronConfig.functionStart);
        r3.add(veyronConfig.functionStart + functionWidth - 1);
    }
}

const personality_t* fixtureGetRdmPersonalities(uint8_t* out_count) {
    *out_count = (uint8_t)VEYRON_NUM_PERSONALITIES;
    return VEYRON_PERSONALITIES;
}

#endif // RAVLIGHT_FIXTURE_VEYRON
