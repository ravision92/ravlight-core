#pragma once
// Built-in effects engine.
//
// Generates synthetic ArtDMX into the registered universe pool when
// dmxConfig.dmxInput == EFFECTS. The render task processes the pool
// exactly as if the bytes had arrived over Art-Net — there is no
// fixture-specific code in here, just RGB pixels stuffed into 512-byte
// per-universe buffers. The render's per-output color_order / grouping /
// invert / gamma / brightness still apply.
//
// Pixels per universe are fixed at 170 (170 × 3 ch = 510 ≤ 512). Each
// universe gets its own RGB slice based on a global pixel index, so a
// strip that spans multiple universes (e.g. Octa with 8 × 325 px → 2 univ
// per strip) sees one continuous animation across the boundary.

#ifdef RAVLIGHT_MODULE_EFFECTS

#include <stdint.h>

enum EffectId : uint8_t {
    EFFECT_SOLID       = 0,   // single color from hue
    EFFECT_RAINBOW     = 1,   // animated hue gradient
    EFFECT_CHASE       = 2,   // scanning bright pixel on a dim base
    EFFECT_FIRE        = 3,   // procedural fire palette per universe
    EFFECT_TWINKLE     = 4,   // random sparkles on a dim base
    EFFECT_SLIDE_RIGHT = 5,   // hard-edge wipe fill, sawtooth left->right then reset
    EFFECT_SLIDE_LEFT  = 6,   // same, right->left
    EFFECT_MIRROR_IN   = 7,   // fill grows from both edges toward the center
    EFFECT_MIRROR_OUT  = 8,   // fill grows from the center toward both edges
    EFFECT_FADE_RIGHT  = 9,   // smooth cosine brightness wave traveling left->right
    EFFECT_FADE_LEFT   = 10,  // same, right->left
    EFFECT_COUNT
};

struct EffectsConfig {
    uint8_t effect;     // EffectId (default SOLID)
    uint8_t speed;      // animation rate, 0-255 (default 128)
    uint8_t r, g, b;    // base colour, 0-255 per channel (solid/chase/twinkle).
                        // Picker writes raw RGB so white is selectable on RGBW;
                        // rainbow ignores this and cycles its own hues; fire has
                        // its own palette.
    uint8_t intensity;  // 0-255 overall brightness multiplier — used by rainbow
                        // and fire only. Solid/chase/twinkle take their brightness
                        // from the colour picker itself.
    uint8_t rgbw_mode;  // 0 = 3-byte stride (RGB strips), 1 = 4-byte stride with
                        // extracted-white W (RGBW strips). Effects engine cannot
                        // satisfy both stride conventions in the same universe,
                        // so the user picks based on their dominant strip type.
    // ── Function channels — written verbatim by the fixture-specific
    // fixtureApplyEffectFunctions() hook after the per-pixel renderer
    // has run. Fixtures without dedicated white/strobe channels just
    // ignore these. Veyron uses all three: 6 white accent channels,
    // strip-strobe rate, accent-strobe rate.
    uint8_t white;        // 0-255 — fills the fixture's "white" channels
    uint8_t strobeRgb;    // 0-255 — strip-side strobe rate
    uint8_t strobeWhite;  // 0-255 — accent-side strobe rate
};

extern EffectsConfig effectsConfig;

void initEffects();
void tickEffects();      // call every loop iteration; internal pacing

// ── Direct rendering entry points ───────────────────────────────────────────
// tickEffects() only runs when dmxConfig.dmxInput == EFFECTS (the standalone
// "generate ArtDMX into the universe pool" mode) — orthogonal to a fixture's
// own personality system. A fixture that wants to run a built-in pattern from
// its OWN DMX personality (e.g. a macro-select channel while still receiving
// live ArtNet/sACN/physical DMX for that channel) calls these instead:
//   1. write effectsConfig.effect/speed/r/g/b/intensity directly
//   2. call tickEffectsPhase() once per its own handleDMX() tick (paces the
//      shared animation phase identically to tickEffects()'s internal rate)
//   3. call renderEffectFrame() to render into its own buffer
// Safe to use alongside tickEffects() being compiled in — the two paths are
// mutually exclusive in practice (dmxInput selects one live data source at a
// time) and share the same effectsConfig/phase state deliberately, the same
// way the web UI already owns effectsConfig for the standalone mode.
bool tickEffectsPhase();   // returns true if paced-enough time has passed (frame is due)
void renderEffectFrame(uint8_t effect, uint16_t world_start, uint16_t count,
                        uint16_t world_total, uint8_t* dst, uint8_t stride);

#endif // RAVLIGHT_MODULE_EFFECTS
