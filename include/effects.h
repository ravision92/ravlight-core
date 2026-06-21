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
    EFFECT_SOLID   = 0,   // single color from hue
    EFFECT_RAINBOW = 1,   // animated hue gradient
    EFFECT_CHASE   = 2,   // scanning bright pixel on a dim base
    EFFECT_FIRE    = 3,   // procedural fire palette per universe
    EFFECT_TWINKLE = 4,   // random sparkles on a dim base
    EFFECT_COUNT
};

struct EffectsConfig {
    uint8_t effect;     // EffectId (default SOLID)
    uint8_t speed;      // animation rate, 0-255 (default 128)
    uint8_t hue;        // base hue 0-255 (default 0 = red)
    uint8_t intensity;  // 0-255 overall brightness multiplier (default 255)
    uint8_t rgbw_mode;  // 0 = 3-byte stride (RGB strips), 1 = 4-byte stride with
                        // extracted-white W (RGBW strips). Effects engine cannot
                        // satisfy both stride conventions in the same universe,
                        // so the user picks based on their dominant strip type.
};

extern EffectsConfig effectsConfig;

void initEffects();
void tickEffects();      // call every loop iteration; internal pacing

#endif // RAVLIGHT_MODULE_EFFECTS
