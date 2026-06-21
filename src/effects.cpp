#ifdef RAVLIGHT_MODULE_EFFECTS
#include <Arduino.h>
#include "effects.h"
#include "config.h"
#include "dmx_manager.h"
#include "esp_log.h"

static const char* TAG = "FX";

EffectsConfig effectsConfig = { (uint8_t)EFFECT_SOLID, 128, 0, 255, 0 };

// 20 fps base rate (50 ms). Faster than this fights the render task for the
// DRAM bus on dense rigs — we measured the LED render dropping from 28 fps
// to 5 fps when effects updated 32 universes at 30 Hz on the QuinLED Octa.
// 20 fps is visually smooth for chase/fire/twinkle and lets the I2S DMA
// breathe between universe pool writes.
static const uint32_t FRAME_MS       = 50;
static const uint16_t MAX_UNIV       = 32;        // matches DMX_MAX_UNIVERSES
static const uint16_t PX_PER_U_RGB   = 170;       // 170 × 3 = 510 ch per universe
static const uint16_t PX_PER_U_RGBW  = 128;       // 128 × 4 = 512 ch per universe

// Refreshed at the top of every tickEffects() from effectsConfig.rgbw_mode so
// the per-effect renderers and put_px() see a consistent stride for the
// whole frame even if the user toggles the mode mid-tick.
static bool     s_rgbw      = false;
static uint16_t PX_PER_U    = PX_PER_U_RGB;

static uint32_t s_last_ms = 0;
static uint32_t s_phase   = 0;   // animation tick counter, advanced by speed
static uint8_t  s_buf[512];

// ── HSV → RGB (8-bit) ──────────────────────────────────────────────────────
// Standard integer formula. ~30 cycles per call on ESP32 @ 240 MHz.
static inline void hsv8_to_rgb(uint8_t h, uint8_t s, uint8_t v,
                                uint8_t* r, uint8_t* g, uint8_t* b) {
    if (s == 0) { *r = *g = *b = v; return; }
    uint8_t region = h / 43;
    uint8_t rem    = (uint8_t)((h - region * 43) * 6);
    uint8_t p = (uint8_t)((v * (255 - s)) >> 8);
    uint8_t q = (uint8_t)((v * (255 - ((s * rem) >> 8))) >> 8);
    uint8_t t = (uint8_t)((v * (255 - ((s * (255 - rem)) >> 8))) >> 8);
    switch (region) {
        case 0:  *r = v; *g = t; *b = p; break;
        case 1:  *r = q; *g = v; *b = p; break;
        case 2:  *r = p; *g = v; *b = t; break;
        case 3:  *r = p; *g = q; *b = v; break;
        case 4:  *r = t; *g = p; *b = v; break;
        default: *r = v; *g = p; *b = q; break;
    }
}

static inline void put_px(uint8_t* buf, uint16_t px, uint8_t r, uint8_t g, uint8_t b) {
    if (s_rgbw) {
        // Extract white: any common component R∩G∩B goes to the W slot, the
        // RGB triplet keeps only the chromatic residual. Solid white → W=255
        // R=G=B=0 (uses the white LED only); pastel colours light the W
        // alongside the chromatic LEDs for a softer mix.
        uint8_t w = r < g ? r : g; if (b < w) w = b;
        uint16_t o = (uint16_t)(px * 4);
        if (o + 3 >= 512) return;
        buf[o] = (uint8_t)(r - w);
        buf[o + 1] = (uint8_t)(g - w);
        buf[o + 2] = (uint8_t)(b - w);
        buf[o + 3] = w;
    } else {
        uint16_t o = (uint16_t)(px * 3);
        if (o + 2 >= 512) return;
        buf[o] = r; buf[o + 1] = g; buf[o + 2] = b;
    }
}

// ── Effect renderers ──────────────────────────────────────────────────────
// Each writes 170 RGB pixels for universe `u` into `buf`. The global pixel
// index is `u * PX_PER_U + i` so multi-universe strips see one continuous
// pattern across universe boundaries.

static void renderSolid(uint16_t /*u*/, uint8_t* buf) {
    uint8_t r, g, b;
    hsv8_to_rgb(effectsConfig.hue, 255, effectsConfig.intensity, &r, &g, &b);
    for (uint16_t i = 0; i < PX_PER_U; i++) put_px(buf, i, r, g, b);
}

static void renderRainbow(uint16_t u, uint8_t* buf) {
    const uint32_t base = u * PX_PER_U + s_phase;
    for (uint16_t i = 0; i < PX_PER_U; i++) {
        uint8_t r, g, b;
        // Hue spans full 256 every 256 pixels — visually one full rainbow per
        // 256 px of physical strip. Shifted by s_phase for animation.
        hsv8_to_rgb((uint8_t)(base + i), 255, effectsConfig.intensity, &r, &g, &b);
        put_px(buf, i, r, g, b);
    }
}

static void renderChase(uint16_t u, uint8_t* buf) {
    // Scanning band of ~8 pixels on a dim background.
    const uint32_t span     = MAX_UNIV * PX_PER_U;     // 5440 px world
    const uint32_t head_g   = (s_phase) % span;        // global head position
    const uint32_t band     = 8;
    uint8_t r0, g0, b0;
    hsv8_to_rgb(effectsConfig.hue, 255, effectsConfig.intensity, &r0, &g0, &b0);
    const uint8_t bg = (uint8_t)((effectsConfig.intensity * 8) / 100);
    for (uint16_t i = 0; i < PX_PER_U; i++) {
        uint32_t global_px = (uint32_t)u * PX_PER_U + i;
        uint32_t d = (global_px >= head_g) ? (global_px - head_g) : (span - (head_g - global_px));
        if (d < band) {
            // attenuation by distance from head
            uint8_t k = (uint8_t)(255 - (d * 255 / band));
            put_px(buf, i,
                   (uint8_t)((r0 * k) >> 8),
                   (uint8_t)((g0 * k) >> 8),
                   (uint8_t)((b0 * k) >> 8));
        } else {
            put_px(buf, i, bg, bg, bg);
        }
    }
}

// Cheap xorshift PRNG seeded per-pixel for deterministic-but-noisy effects.
static inline uint32_t pix_rand(uint32_t global_px, uint32_t t) {
    uint32_t x = global_px * 2654435761u ^ t * 1597334677u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return x;
}

static void renderFire(uint16_t u, uint8_t* buf) {
    // Hot palette — red→orange→yellow→white. Cooling factor depends on
    // distance from the "bottom" (low pixel index = hottest).
    const uint32_t t = s_phase / 4;          // slow flicker compared to other effects
    for (uint16_t i = 0; i < PX_PER_U; i++) {
        uint32_t global_px = (uint32_t)u * PX_PER_U + i;
        uint32_t rng = pix_rand(global_px, t);
        uint8_t heat = (uint8_t)((rng & 0xFF) ^ ((rng >> 8) & 0xFF));
        // Cool with i — top pixels less hot.
        uint16_t cool = (uint16_t)((i * 200) / PX_PER_U);
        heat = (heat > cool) ? (uint8_t)(heat - cool) : 0;
        heat = (uint8_t)((heat * effectsConfig.intensity) / 255);
        uint8_t r, g, b;
        if (heat < 85)       { r = (uint8_t)(heat * 3); g = 0;                 b = 0; }
        else if (heat < 170) { r = 255;                 g = (uint8_t)((heat - 85) * 3); b = 0; }
        else                 { r = 255;                 g = 255;                 b = (uint8_t)((heat - 170) * 3); }
        put_px(buf, i, r, g, b);
    }
}

static void renderTwinkle(uint16_t u, uint8_t* buf) {
    // Soft random sparkles on a dim hue base.
    uint8_t r0, g0, b0;
    hsv8_to_rgb(effectsConfig.hue, 255, effectsConfig.intensity, &r0, &g0, &b0);
    const uint8_t bg = (uint8_t)((effectsConfig.intensity * 6) / 100);
    const uint32_t t = s_phase / 2;
    for (uint16_t i = 0; i < PX_PER_U; i++) {
        uint32_t global_px = (uint32_t)u * PX_PER_U + i;
        uint32_t rng = pix_rand(global_px, t);
        uint8_t spark = (uint8_t)(rng & 0xFF);
        if (spark < 250) {
            put_px(buf, i, bg, bg, bg);
        } else {
            uint8_t k = (uint8_t)(((spark - 250) * 51));  // 0..255 ramp
            put_px(buf, i,
                   (uint8_t)((r0 * k) >> 8),
                   (uint8_t)((g0 * k) >> 8),
                   (uint8_t)((b0 * k) >> 8));
        }
    }
}

void initEffects() {
    s_last_ms = millis();
    s_phase   = 0;
    ESP_LOGI(TAG, "effects engine ready (default: solid red)");
}

void tickEffects() {
    if (dmxConfig.dmxInput != EFFECTS) return;

    uint32_t now = millis();
    if (now - s_last_ms < FRAME_MS) return;
    s_last_ms = now;
    // Phase advance scaled by speed: speed=128 → 1 tick/frame (~30 px/s on
    // rainbow), speed=255 → ~2 ticks/frame, speed=0 → frozen.
    s_phase += (uint32_t)(effectsConfig.speed + 1) >> 6;   // 0..4 ticks/frame

    // Refresh stride mode for this frame. Renderers read PX_PER_U and put_px
    // checks s_rgbw — keeping both consistent inside one frame.
    s_rgbw   = (effectsConfig.rgbw_mode != 0);
    PX_PER_U = s_rgbw ? PX_PER_U_RGBW : PX_PER_U_RGB;
    // Zero the buffer so unused tail bytes are deterministic — without this
    // the leftover channels from the previous frame's stride end up shifted
    // into the wrong pixels on the next render.
    memset(s_buf, 0, sizeof(s_buf));

    uint8_t effect = effectsConfig.effect;
    if (effect >= EFFECT_COUNT) effect = EFFECT_SOLID;

    // Iterate only registered universes — every spurious injectDmxUniverse on
    // an unregistered id still takes dmxBufferMutex, and on a live render
    // the cumulative mutex/IRQ traffic was enough to push the I2S WAIT
    // measurably longer (28 fps → 5 fps on Octa with 32 blind probes/tick).
    uint8_t n_univ = dmxUniverseCount();

    // SOLID is the same for every universe → render once, broadcast.
    if (effect == EFFECT_SOLID) renderSolid(0, s_buf);

    for (uint8_t i = 0; i < n_univ; i++) {
        uint16_t u = dmxUniverseAt(i);
        if (effect != EFFECT_SOLID) {
            switch (effect) {
                case EFFECT_RAINBOW: renderRainbow(u, s_buf); break;
                case EFFECT_CHASE:   renderChase  (u, s_buf); break;
                case EFFECT_FIRE:    renderFire   (u, s_buf); break;
                case EFFECT_TWINKLE: renderTwinkle(u, s_buf); break;
                default:             renderSolid  (u, s_buf); break;
            }
        }
        injectDmxUniverse(u, s_buf, 512);
    }
}

#endif // RAVLIGHT_MODULE_EFFECTS
