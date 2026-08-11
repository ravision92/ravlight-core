#ifdef RAVLIGHT_FIXTURE_VEYRON
#include "fixtures/veyron/dmx_fixture.h"
#include "fixtures/veyron/fixture.h"
#include "fixtures/veyron/fixture_ids.h"
#include "fixtures/veyron/personalities.h"
#include "core/dmx_patch.h"
#include "core/led_output.h"
#include "core/p9813.h"
#include "dmx_manager.h"
#include "config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "driver/rmt.h"
#include "effects.h"
#include <math.h>

static const char* TAG = "FIXTURE";

static inline uint32_t now_ms() {
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static inline float fmap(float x, float in_min, float in_max, float out_min, float out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

static bool tickStatusOverlay();   // status LED module, defined below

static uint32_t startTimeHighlite  = 0;
static bool     isHighlight     = false;

bool handleDMXenable = true;
static bool led1State = false;
static bool led2State = false;

static led_output_t strip1;
static p9813_t      strip2;

static uint8_t  strobeRate1 = 0;
static uint8_t  strobeRate2 = 0;
static uint16_t dimcurve    = 0;

static uint32_t lastTime1   = 0;
static uint32_t lastTime2   = 0;
static uint32_t currentTime = 0;

static patch_state_t veyron_patch;

// RDM_PID_DMX_START_ADDRESS tracking — see the sync block in handleDMX().
// 0 = uninitialized sentinel (never equals a valid 1-based DMX address).
static uint16_t s_dmxLastStartAddr = 0;

extern uint8_t dmxBuffer[];

void initFixture() {
    veyron_patch.table           = VEYRON_PERSONALITIES;
    veyron_patch.personality_idx = (uint8_t)(veyronConfig.personality - 1);
    veyron_patch.universe        = (uint8_t)dmxConfig.startUniverse;
    veyron_patch.section_start[VEYRON_SEC_STRIP]  = veyronConfig.rgbwStart;
    veyron_patch.section_start[VEYRON_SEC_ACCENT] = veyronConfig.whiteStart;
    veyron_patch.section_start[VEYRON_SEC_STROBE] = veyronConfig.functionStart;

    dimcurve = veyronConfig.DimCurves;

#ifdef RAVLIGHT_MODULE_DMX_PHYSICAL
    // Seed esp_dmx's RDM_PID_DMX_START_ADDRESS/PERSONALITY with our own
    // config from boot — esp_dmx's own internal personality state defaults
    // to 1 (its own NVS, separate from ours) and nothing else ever tells it
    // otherwise. Without this, handleDMX()'s "apply an RDM console change"
    // sync block below sees "esp_dmx says 1, we say N" forever and force-
    // reverts every non-default personality back to 1 on the very first frame.
    dmxSetStartAddress(veyronConfig.rgbwStart);
    s_dmxLastStartAddr = veyronConfig.rgbwStart;
    dmxSetCurrentPersonality((uint8_t)veyronConfig.personality);
#endif

    led_output_init(&strip1, HW_LED_OUTPUT_PINS[0], VEYRON_NUM_PIXELS_1, RMT_CHANNEL_0, 4, 3);
    p9813_init(&strip2, HW_PIN_P9813_DATA, HW_PIN_P9813_CLK, VEYRON_NUM_PIXELS_2);
    led_output_clear(&strip1);
    led_output_flush(&strip1);
    p9813_clear(&strip2);
    p9813_flush(&strip2);
}

// fixtureConfigDeserialize() (fixture_config.cpp) only updates veyronConfig —
// it doesn't know about veyron_patch, which is private to this file and is
// what handleDMX()'s getChannelById()/getChannelBlockById() actually read
// from. Without this, a personality/address change saved via /api/config
// reported "saved" but the renderer kept using the old channel offsets
// until the next reboot re-ran initFixture(). Mirrors initFixture()'s sync
// block; the client (fixture.js) already computes accent/strobe addresses
// consistent with the chosen personality, so we can trust veyronConfig
// as-is rather than re-deriving via relayoutSectionAddresses().
void applyVeyronConfigLive() {
    veyron_patch.personality_idx = (uint8_t)(veyronConfig.personality - 1);
    veyron_patch.section_start[VEYRON_SEC_STRIP]  = veyronConfig.rgbwStart;
    veyron_patch.section_start[VEYRON_SEC_ACCENT] = veyronConfig.whiteStart;
    veyron_patch.section_start[VEYRON_SEC_STROBE] = veyronConfig.functionStart;
    dimcurve = veyronConfig.DimCurves;
#ifdef RAVLIGHT_MODULE_DMX_PHYSICAL
    dmxSetStartAddress(veyronConfig.rgbwStart);
    s_dmxLastStartAddr = veyronConfig.rgbwStart;
    // Keep esp_dmx's own RDM personality state in sync with a web UI change
    // too — same reasoning as the boot-time seed in initFixture(): without
    // this, the RDM-sync block in handleDMX() sees esp_dmx still reporting
    // the old personality and force-reverts this save right back.
    dmxSetCurrentPersonality((uint8_t)veyronConfig.personality);
#endif
}

// Recompute accent/function section addresses from the CURRENT personality's
// own channel widths, anchored at the strip's start address (rgbwStart,
// left untouched — it's the one address the operator/RDM actually sets).
// Needed because each personality has a completely different footprint per
// section (e.g. Personality 1: strip 120ch+accent 6ch vs Personality 9: cast
// 3ch+white 1ch) — carrying over the OLD personality's absolute addresses
// puts accent/function channels outside the new, much smaller footprint.
static void relayoutSectionAddresses() {
    const personality_t& pers = VEYRON_PERSONALITIES[veyron_patch.personality_idx];
    uint16_t stripWidth = 0, accentWidth = 0;
    for (uint8_t i = 0; i < pers.n_channels; i++) {
        const dmx_channel_t& ch = pers.channels[i];
        if (ch.section == VEYRON_SEC_STRIP)  stripWidth  += ch.count;
        if (ch.section == VEYRON_SEC_ACCENT) accentWidth += ch.count;
    }
    uint16_t rgbw     = veyron_patch.section_start[VEYRON_SEC_STRIP];
    uint16_t accent   = rgbw + stripWidth;
    uint16_t function = accent + accentWidth;
    setFixtureAddresses(rgbw, accent, function);
}

void setPersonality(FixturePersonality personality) {
    veyronConfig.personality     = personality;
    veyron_patch.personality_idx = (uint8_t)(personality - 1);
    relayoutSectionAddresses();
#ifdef RAVLIGHT_MODULE_DMX_PHYSICAL
    // Keep esp_dmx's own RDM personality state in sync — see initFixture()'s
    // comment. Idempotent when this call originated from the RDM-sync block
    // itself (console already told esp_dmx this value).
    dmxSetCurrentPersonality((uint8_t)personality);
#endif
    ESP_LOGI(TAG, "DMX personality set: %d (addr %u/%u/%u)", personality,
             veyron_patch.section_start[VEYRON_SEC_STRIP],
             veyron_patch.section_start[VEYRON_SEC_ACCENT],
             veyron_patch.section_start[VEYRON_SEC_STROBE]);
}

void setDimCurve(uint16_t curve) {
    dimcurve = curve;
    veyronConfig.DimCurves = curve;
}

void setFixtureAddresses(int rgbwStart, int whStart, int functionStart) {
    veyron_patch.section_start[VEYRON_SEC_STRIP]  = (uint16_t)rgbwStart;
    veyron_patch.section_start[VEYRON_SEC_ACCENT] = (uint16_t)whStart;
    veyron_patch.section_start[VEYRON_SEC_STROBE] = (uint16_t)functionStart;
    veyronConfig.rgbwStart     = (uint16_t)rgbwStart;
    veyronConfig.whiteStart    = (uint16_t)whStart;
    veyronConfig.functionStart = (uint16_t)functionStart;
#ifdef RAVLIGHT_MODULE_DMX_PHYSICAL
    // Keep esp_dmx's RDM_PID_DMX_START_ADDRESS in sync with a manual web UI
    // change too, so an RDM GET right after doesn't read a stale value.
    dmxSetStartAddress((uint16_t)rgbwStart);
    s_dmxLastStartAddr = (uint16_t)rgbwStart;
#endif
}

void stopDMX() {
    handleDMXenable = false;
    led_output_clear(&strip1);
    led_output_flush(&strip1);
    p9813_clear(&strip2);
    p9813_flush(&strip2);
}

void startDMX() {
    handleDMXenable = true;
    led_output_clear(&strip1);
    led_output_flush(&strip1);
    p9813_clear(&strip2);
    p9813_flush(&strip2);
}

void handleDMX() {
    // Apply any pending ArtSync swap so the universe pool's active buffer
    // is current before any downstream code reads it. Veyron's own
    // personalities still read from the legacy dmxBuffer (which is
    // written synchronously and isn't affected by sync mode), but
    // /dmxdata — used by the DMX Monitor page — reads via
    // getUniverseData() and *was* always showing zeros under
    // ArtSync-emitting controllers (Resolume default). Same fix Axon
    // applies at the top of its render.
    dmxApplyPendingSwap();

#ifdef RAVLIGHT_MODULE_DMX_PHYSICAL
    // A console can change personality remotely via RDM_PID_DMX_PERSONALITY
    // SET; esp_dmx tracks that index internally but doesn't know how to poke
    // our own veyronConfig.personality. Poll it here so an RDM personality
    // change actually takes effect instead of being cosmetic RDM metadata.
    // Numbering is 1-based on both sides (esp_dmx personality_num / our enum).
    uint8_t rdmPers = dmxGetCurrentPersonality();
    if (rdmPers >= 1 && rdmPers <= VEYRON_NUM_PERSONALITIES &&
        rdmPers != (uint8_t)veyronConfig.personality) {
        setPersonality((FixturePersonality)rdmPers);
    }

    // Same reasoning for RDM_PID_DMX_START_ADDRESS: esp_dmx's default
    // responder handler accepts a console SET and stores it internally, but
    // has no idea Veyron actually uses three independent section addresses
    // (strip/accent/strobe) instead of one contiguous footprint. Shift all
    // three by the same delta so the whole personality moves as one block —
    // normal RDM semantics for "the fixture's start address changed".
    uint16_t rdmAddr = dmxGetStartAddress();
    if (s_dmxLastStartAddr != 0 && rdmAddr != 0 && rdmAddr != s_dmxLastStartAddr) {
        int16_t delta = (int16_t)rdmAddr - (int16_t)s_dmxLastStartAddr;
        int32_t newStrip  = (int32_t)veyron_patch.section_start[VEYRON_SEC_STRIP]  + delta;
        int32_t newAccent = (int32_t)veyron_patch.section_start[VEYRON_SEC_ACCENT] + delta;
        int32_t newStrobe = (int32_t)veyron_patch.section_start[VEYRON_SEC_STROBE] + delta;
        // Reject the whole shift if it would push any section out of the
        // valid 1-512 DMX channel range — a partial/clamped apply would
        // corrupt the gap between sections instead of preserving it.
        if (newStrip  < 1 || newStrip  > 512 ||
            newAccent < 1 || newAccent > 512 ||
            newStrobe < 1 || newStrobe > 512) {
            ESP_LOGW(TAG, "RDM DMX_START_ADDRESS %u rejected: would push a section "
                     "out of 1-512 range (strip=%ld accent=%ld strobe=%ld)",
                     rdmAddr, (long)newStrip, (long)newAccent, (long)newStrobe);
            dmxSetStartAddress(s_dmxLastStartAddr);   // tell esp_dmx to keep reporting the old value
        } else {
            setFixtureAddresses(newStrip, newAccent, newStrobe);
            ESP_LOGI(TAG, "RDM DMX_START_ADDRESS set: %u -> %u (delta %d)",
                     s_dmxLastStartAddr, rdmAddr, delta);
            // setFixtureAddresses() already updates s_dmxLastStartAddr to rdmAddr.
        }
    }
#endif

    // Highlight (manual identify) and the status LED (net/OTA indicator)
    // both take exclusive control of the outputs — render one of them and
    // skip the normal DMX render entirely for this frame, instead of
    // drawing DMX first and painting over it. A console flooding channels
    // during an OTA upload or boot-time reconnect must never fight the
    // overlay for the strip.
    if (isHighlight) {
        higliteSequence();
    } else if (!tickStatusOverlay()) {
        switch (veyronConfig.personality) {
            case PERSONALITY_1: handleDMXPersonality1(); break;
            case PERSONALITY_2: handleDMXPersonality2(); break;
            case PERSONALITY_3: handleDMXPersonality3(); break;
            case PERSONALITY_4: handleDMXPersonality4(); break;
            case PERSONALITY_5: handleDMXPersonality5(); break;
            case PERSONALITY_6: handleDMXPersonality6(); break;
            case PERSONALITY_7: handleDMXPersonality7(); break;
            case PERSONALITY_8: handleDMXPersonality8(); break;
            case PERSONALITY_9: handleDMXPersonality9(); break;
            default: ESP_LOGW(TAG, "Unknown DMX personality"); break;
        }
    }
    currentTime = now_ms();
}

// The P9813 accent pair physically drives 6 independent white COB LEDs, not
// 2 RGB pixels (see fixture_ids.h ID_ACCENT_WHITE_1..6) — read the 6 scalar
// channels into a flat array so callers can keep indexing a[i*3+0/1/2]
// exactly as before (same wire layout, only the channel metadata changed).
static void readAccentWhites(uint8_t a[6]) {
    a[0] = getChannelById(&veyron_patch, dmxBuffer, ID_ACCENT_WHITE_1);
    a[1] = getChannelById(&veyron_patch, dmxBuffer, ID_ACCENT_WHITE_2);
    a[2] = getChannelById(&veyron_patch, dmxBuffer, ID_ACCENT_WHITE_3);
    a[3] = getChannelById(&veyron_patch, dmxBuffer, ID_ACCENT_WHITE_4);
    a[4] = getChannelById(&veyron_patch, dmxBuffer, ID_ACCENT_WHITE_5);
    a[5] = getChannelById(&veyron_patch, dmxBuffer, ID_ACCENT_WHITE_6);
}

static uint8_t apply_dimming(uint8_t v, uint16_t curve) {
    float n = v / 255.0f;
    float s;
    switch (curve) {
        case LINEAR:         s = n; break;
        case SQUARE:         s = n * n; break;
        case INVERSE_SQUARE: s = sqrtf(n); break;
        case S_CURVE:        s = 1.0f / (1.0f + expf(-12.0f * (n - 0.5f))); break;
        default:             s = n; break;
    }
    return (uint8_t)(s * 255.0f);
}

// Master Dimmer/Intensity — a scale applied on top of the per-pixel/per-white
// output, same idea as a real fixture's overall intensity fader independent
// of the color/pixel data itself. 255 = no attenuation (also the channel's
// default_val, so an unpatched/untouched channel doesn't dim anything).
static inline uint8_t scale8(uint8_t v, uint8_t scale) {
    return (uint8_t)(((uint16_t)v * scale) / 255);
}

// ── Shutter channel (Strobe/Random Strobe/Pulse Open/Pulse Close) ──────────
// Classic single-DMX-byte scheme: Open buffers only at the two ends (0-10,
// 250-255), four wide function zones between them (~60 DMX steps of speed
// resolution each).
enum class ShutterFn : uint8_t { OPEN, STROBE, RANDOM_STROBE, PULSE_OPEN, PULSE_CLOSE };

static ShutterFn decodeShutterFn(uint8_t v, uint8_t* zonePos) {
    if (v <= 10)  { *zonePos = 0;       return ShutterFn::OPEN; }
    if (v <= 70)  { *zonePos = v - 11;  return ShutterFn::STROBE; }
    if (v <= 130) { *zonePos = v - 71;  return ShutterFn::RANDOM_STROBE; }
    if (v <= 190) { *zonePos = v - 131; return ShutterFn::PULSE_OPEN; }
    if (v <= 249) { *zonePos = v - 191; return ShutterFn::PULSE_CLOSE; }
    *zonePos = 0; return ShutterFn::OPEN;
}

static inline uint32_t shutterInterval(uint8_t zonePos) {
    uint8_t rate = (uint8_t)(1 + ((uint32_t)zonePos * 254) / 59);   // ~0-59 -> 1-255
    float strobecurve = VEYRON_STROBE_CURVE_A * powf(rate, VEYRON_STROBE_CURVE_B);
    return (uint32_t)fmap(strobecurve, 1.0f, 255.0f,
                          (float)VEYRON_STROBE_RATE_MIN, (float)VEYRON_STROBE_RATE_MAX);
}

// Shared state machine for one shutter channel (strip or accent each keep
// their own lastTime/randInterval statics — see applyStrobe()/applyStrobe2()
// below). ledState is the boolean the personality handlers already gate
// pixel writes on.
static void applyShutter(uint8_t value, uint32_t& lastTime, bool& ledState, uint32_t& randInterval) {
    uint8_t zonePos;
    ShutterFn fn = decodeShutterFn(value, &zonePos);
    if (fn == ShutterFn::OPEN) { ledState = true; return; }

    uint32_t interval = shutterInterval(zonePos);

    switch (fn) {
        case ShutterFn::STROBE:
            // Thin flash from an OFF baseline — the original bare-rate
            // channel's proven timing, unchanged.
            if (lastTime + interval > currentTime) {
                ledState = false;
            } else if (lastTime + interval + VEYRON_STROBE_DURATION < currentTime) {
                lastTime = currentTime;
            } else {
                ledState = true;
            }
            break;
        case ShutterFn::RANDOM_STROBE:
            // Same thin-flash shape, but each cycle's wait is re-rolled
            // (base interval, jittered ±50%) instead of fixed.
            if (lastTime + randInterval > currentTime) {
                ledState = false;
            } else if (lastTime + randInterval + VEYRON_STROBE_DURATION < currentTime) {
                lastTime     = currentTime;
                randInterval = interval / 2 + (esp_random() % (interval + 1));
            } else {
                ledState = true;
            }
            break;
        case ShutterFn::PULSE_OPEN: {
            // Fat 50%-duty square wave from an OFF-first baseline — a
            // longer, more visible "opening" than Strobe's thin flick.
            uint32_t onTime = interval / 2;
            if (lastTime + interval < currentTime) lastTime = currentTime;
            ledState = (currentTime >= lastTime + onTime);
            break;
        }
        case ShutterFn::PULSE_CLOSE: {
            // Same 50%-duty square wave, phase-inverted — ON-first baseline
            // with a periodic blackout dip.
            uint32_t onTime = interval / 2;
            if (lastTime + interval < currentTime) lastTime = currentTime;
            ledState = !(currentTime >= lastTime + onTime);
            break;
        }
        default: ledState = true; break;
    }
}

void applyStrobe(uint8_t strobeRate) {
    static uint32_t randInterval1 = 0;
    applyShutter(strobeRate, lastTime1, led1State, randInterval1);
}

void applyStrobe2(uint8_t strobeRate) {
    static uint32_t randInterval2 = 0;
    applyShutter(strobeRate, lastTime2, led2State, randInterval2);
}

// ── Shared macro engines (RGB strip / accent white / mask-preserving) ──────
static inline uint32_t whitePixRand(uint32_t idx, uint32_t t) {
    uint32_t x = idx * 2654435761u ^ t * 1597334677u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return x;
}

enum class WhiteMacro : uint8_t {
    IDLE, BREATHE, CHASE, SUPERCAR, TWINKLE, FLICKER,
    FADE_RIGHT, FADE_LEFT, SLIDE_RIGHT, SLIDE_LEFT, MIRROR_IN, MIRROR_OUT
};

static WhiteMacro decodeWhiteMacro(uint8_t v) {
    if (v < 10)  return WhiteMacro::IDLE;
    if (v < 33)  return WhiteMacro::BREATHE;
    if (v < 56)  return WhiteMacro::CHASE;
    if (v < 79)  return WhiteMacro::SUPERCAR;
    if (v < 102) return WhiteMacro::TWINKLE;
    if (v < 124) return WhiteMacro::FLICKER;
    if (v < 146) return WhiteMacro::FADE_RIGHT;
    if (v < 168) return WhiteMacro::FADE_LEFT;
    if (v < 190) return WhiteMacro::SLIDE_RIGHT;
    if (v < 212) return WhiteMacro::SLIDE_LEFT;
    if (v < 234) return WhiteMacro::MIRROR_IN;
    return WhiteMacro::MIRROR_OUT;
}

// Own pacing/phase, independent of effects.cpp's tickEffectsPhase() (that one
// paces the RGB macro's engine calls; this one has nothing to do with it).
static uint32_t s_whiteMacroPhase  = 0;
static uint32_t s_whiteMacroLastMs = 0;

static void renderWhiteMacro(WhiteMacro macro, uint8_t speed, uint8_t out[6]) {
    uint32_t now = now_ms();
    if (now - s_whiteMacroLastMs >= 50) {          // ~20 fps, matches effects.cpp's FRAME_MS
        s_whiteMacroLastMs = now;
        s_whiteMacroPhase += (uint32_t)(speed + 1) >> 6;
    }
    uint32_t t = s_whiteMacroPhase;
    switch (macro) {
        case WhiteMacro::IDLE:
            for (int i = 0; i < 6; i++) out[i] = 0;
            break;
        case WhiteMacro::BREATHE: {
            uint8_t v = (uint8_t)((sinf(t * 0.05f) * 0.5f + 0.5f) * 255.0f);
            for (int i = 0; i < 6; i++) out[i] = v;
            break;
        }
        case WhiteMacro::CHASE: {
            uint32_t pos = t % 6;
            for (int i = 0; i < 6; i++) out[i] = ((uint32_t)i == pos) ? 255 : 0;
            break;
        }
        case WhiteMacro::SUPERCAR: {
            // Ping-pong scanner across the 6 whites (Knight Rider-style).
            uint32_t idx = t % 10;
            uint32_t pos = (idx <= 5) ? idx : 10 - idx;
            for (int i = 0; i < 6; i++) {
                uint32_t d = ((uint32_t)i > pos) ? ((uint32_t)i - pos) : (pos - (uint32_t)i);
                out[i] = (d == 0) ? 255 : (d == 1 ? 90 : 0);
            }
            break;
        }
        case WhiteMacro::TWINKLE: {
            for (int i = 0; i < 6; i++) {
                uint32_t rng = whitePixRand((uint32_t)i, t);
                out[i] = ((rng & 0xFF) > 220) ? 255 : 0;
            }
            break;
        }
        case WhiteMacro::FLICKER: {
            for (int i = 0; i < 6; i++) {
                uint32_t rng = whitePixRand((uint32_t)i, t / 2);
                out[i] = (uint8_t)(150 + (rng & 0x65));   // candle-ish upper-range noise
            }
            break;
        }
        case WhiteMacro::FADE_RIGHT:
        case WhiteMacro::FADE_LEFT: {
            // Smooth cosine brightness wave across the 6 whites — continuous,
            // unlike the hard block edges of Slide/the 3-level Supercar.
            bool rightward = (macro == WhiteMacro::FADE_RIGHT);
            int32_t shift = rightward ? (int32_t)t : -(int32_t)t;
            for (int i = 0; i < 6; i++) {
                float ang = 2.0f * (float)M_PI * ((float)((int32_t)i + shift) / 6.0f);
                out[i] = (uint8_t)((cosf(ang) * 0.5f + 0.5f) * 255.0f);
            }
            break;
        }
        case WhiteMacro::SLIDE_RIGHT:
        case WhiteMacro::SLIDE_LEFT: {
            // Same sawtooth-wipe idea as effects.cpp's renderSlide, scaled to 6.
            uint32_t boundary = t % 6;
            bool rightward = (macro == WhiteMacro::SLIDE_RIGHT);
            for (uint32_t i = 0; i < 6; i++) {
                bool lit = rightward ? (i < boundary) : (i >= (6 - boundary));
                out[i] = lit ? 255 : 0;
            }
            break;
        }
        case WhiteMacro::MIRROR_IN:
        case WhiteMacro::MIRROR_OUT: {
            // Same symmetric-wipe idea as effects.cpp's renderMirror, half=3.
            uint32_t pos = t % 3;
            bool inward = (macro == WhiteMacro::MIRROR_IN);
            for (uint32_t i = 0; i < 6; i++) {
                uint32_t distFromCenter = (i < 3) ? (3 - i) : (i - 3);
                uint32_t distFromEdge   = (i < 3) ? i : (5 - i);
                bool lit = inward ? (distFromEdge <= pos) : (distFromCenter <= pos);
                out[i] = lit ? 255 : 0;
            }
            break;
        }
    }
}

static void renderWhiteMacroToAccent(uint8_t macroByte, uint8_t speed, uint8_t out[6]) {
    renderWhiteMacro(decodeWhiteMacro(macroByte), speed, out);
}

#ifdef RAVLIGHT_MODULE_EFFECTS
// RGB macro -> strip buffer (RGBW + Macro personality only — global color, no
// per-pixel identity). Range buckets: Idle / Rainbow / Fire (self-colored,
// ignore the color block) / Solid / Chase / Twinkle / Fade Right / Fade Left
// / Slide Right / Slide Left / Mirror In / Mirror Out (color-driven, use the
// color block — substituting full white if left at (0,0,0) so browsing
// macros without ever setting a color isn't dark). Returns true if a new
// frame was rendered into `out` this tick (paced by tickEffectsPhase()) —
// false means "idle" (caller should clear/fall back to manual) or "not due yet".
static bool renderRgbMacroToStrip(uint8_t macroByte, const uint8_t* color, uint8_t speed,
                                   uint8_t out[VEYRON_NUM_PIXELS_1 * 3]) {
    if (macroByte < 10) return false;
    uint8_t effect;
    bool colorDriven;
    if      (macroByte < 33)  { effect = EFFECT_RAINBOW;     colorDriven = false; }
    else if (macroByte < 56)  { effect = EFFECT_FIRE;        colorDriven = false; }
    else if (macroByte < 79)  { effect = EFFECT_SOLID;       colorDriven = true;  }
    else if (macroByte < 102) { effect = EFFECT_CHASE;       colorDriven = true;  }
    else if (macroByte < 124) { effect = EFFECT_TWINKLE;     colorDriven = true;  }
    else if (macroByte < 146) { effect = EFFECT_FADE_RIGHT;  colorDriven = true;  }
    else if (macroByte < 168) { effect = EFFECT_FADE_LEFT;   colorDriven = true;  }
    else if (macroByte < 190) { effect = EFFECT_SLIDE_RIGHT; colorDriven = true;  }
    else if (macroByte < 212) { effect = EFFECT_SLIDE_LEFT;  colorDriven = true;  }
    else if (macroByte < 234) { effect = EFFECT_MIRROR_IN;   colorDriven = true;  }
    else                      { effect = EFFECT_MIRROR_OUT;  colorDriven = true;  }

    effectsConfig.speed     = speed;
    effectsConfig.intensity = 255;
    if (colorDriven && color) {
        uint8_t r = color[0], g = color[1], b = color[2];
        if (r == 0 && g == 0 && b == 0) { r = g = b = 255; }
        effectsConfig.r = r; effectsConfig.g = g; effectsConfig.b = b;
    }

    if (!tickEffectsPhase()) return false;
    renderEffectFrame(effect, 0, VEYRON_NUM_PIXELS_1, VEYRON_NUM_PIXELS_1, out, 3);
    return true;
}
#endif

// ── Mask-preserving movement macro (Zone Macro / Pixel Macro) ──────────────
// Shared by Mirror/Grouped + Macro (20 logical zones) and Full Pixel + Macro
// (40 physical pixels, no zone mapping needed) — a brightness mask (0-255
// per element) multiplies the operator's own patched color instead of
// replacing it with a shared macro color. Rainbow/Fire remain the
// self-colored exception (they override color entirely, same as the RGB
// macro above). Own decode table (no "Solid" bucket — there's no shared
// macro-color channel to be solid *with*, idle already shows the patched
// colors statically).
enum class ZoneMacro : uint8_t {
    IDLE, RAINBOW, FIRE,
    CHASE, TWINKLE, FADE_RIGHT, FADE_LEFT,
    SLIDE_RIGHT, SLIDE_LEFT, MIRROR_IN, MIRROR_OUT
};

static ZoneMacro decodeZoneMacro(uint8_t v) {
    if (v < 10)  return ZoneMacro::IDLE;
    if (v < 35)  return ZoneMacro::RAINBOW;
    if (v < 60)  return ZoneMacro::FIRE;
    if (v < 85)  return ZoneMacro::CHASE;
    if (v < 110) return ZoneMacro::TWINKLE;
    if (v < 135) return ZoneMacro::FADE_RIGHT;
    if (v < 160) return ZoneMacro::FADE_LEFT;
    if (v < 184) return ZoneMacro::SLIDE_RIGHT;
    if (v < 208) return ZoneMacro::SLIDE_LEFT;
    if (v < 232) return ZoneMacro::MIRROR_IN;
    return ZoneMacro::MIRROR_OUT;
}

// Computes a 0-255 brightness mask per element for the color-driven patterns
// (CHASE/TWINKLE/FADE/SLIDE/MIRROR) — RAINBOW/FIRE are handled separately by
// the caller (self-colored, bypass this mask entirely). `count` is 20 for
// Zone Macro or 40 for Pixel Macro; `phase`/`lastMs` are the caller's own
// pacing statics so Zone/Pixel Macro speeds aren't accidentally coupled to
// each other or to White Macro, even though all may share the same DMX
// Macro Speed value.
static void renderMovementMask(ZoneMacro macro, uint8_t speed, uint8_t* mask, uint8_t count,
                                uint32_t& phase, uint32_t& lastMs) {
    uint32_t now = now_ms();
    if (now - lastMs >= 50) {
        lastMs = now;
        phase += (uint32_t)(speed + 1) >> 6;
    }
    uint32_t t = phase;
    switch (macro) {
        case ZoneMacro::CHASE: {
            uint32_t band = count / 5; if (band < 2) band = 2;
            uint32_t pos = t % count;
            for (uint32_t i = 0; i < count; i++) {
                uint32_t d = (i >= pos) ? (i - pos) : (count - pos + i);
                mask[i] = (d < band) ? (uint8_t)(255 - (d * 255 / band)) : 0;
            }
            break;
        }
        case ZoneMacro::TWINKLE: {
            for (uint32_t i = 0; i < count; i++) {
                uint32_t rng = whitePixRand(i, t / 2);
                mask[i] = ((rng & 0xFF) > 200) ? 255 : 40;
            }
            break;
        }
        case ZoneMacro::FADE_RIGHT:
        case ZoneMacro::FADE_LEFT: {
            bool rightward = (macro == ZoneMacro::FADE_RIGHT);
            int32_t shift = rightward ? (int32_t)t : -(int32_t)t;
            for (int32_t i = 0; i < count; i++) {
                float ang = 2.0f * (float)M_PI * ((float)(i + shift) / (float)count);
                mask[i] = (uint8_t)((cosf(ang) * 0.5f + 0.5f) * 255.0f);
            }
            break;
        }
        case ZoneMacro::SLIDE_RIGHT:
        case ZoneMacro::SLIDE_LEFT: {
            uint32_t boundary = t % count;
            bool rightward = (macro == ZoneMacro::SLIDE_RIGHT);
            for (uint32_t i = 0; i < count; i++) {
                bool lit = rightward ? (i < boundary) : (i >= (count - boundary));
                mask[i] = lit ? 255 : 0;
            }
            break;
        }
        case ZoneMacro::MIRROR_IN:
        case ZoneMacro::MIRROR_OUT: {
            uint32_t half = count / 2;
            uint32_t pos = t % half;
            bool inward = (macro == ZoneMacro::MIRROR_IN);
            for (uint32_t i = 0; i < count; i++) {
                uint32_t distFromCenter = (i < half) ? (half - i) : (i - half);
                uint32_t distFromEdge   = (i < half) ? i : (count - 1 - i);
                bool lit = inward ? (distFromEdge <= pos) : (distFromCenter <= pos);
                mask[i] = lit ? 255 : 0;
            }
            break;
        }
        default:
            for (uint8_t i = 0; i < count; i++) mask[i] = 255;
            break;
    }
}

// Own pacing/phase per macro instance — Zone Macro (Mirror/Grouped, 20
// elements) and Pixel Macro (Full Pixel, 40 elements) never run
// simultaneously (one personality at a time) but keep separate statics for
// clarity/safety.
static uint32_t s_zoneMacroPhase   = 0;
static uint32_t s_zoneMacroLastMs  = 0;
static uint32_t s_pixelMacroPhase  = 0;
static uint32_t s_pixelMacroLastMs = 0;

// Renders the 20-zone strip (Mirror or Grouped layout) with the Zone Macro
// applied. `mirrorMode` selects the physical mapping: true = zone i mirrors
// to physical pixels i and 39-i; false = zone i groups to 2i and 2i+1.
static void renderZoneStrip(const uint8_t* zoneRGB, bool mirrorMode, ZoneMacro macro,
                             uint8_t speed, uint8_t masterStrip) {
    if (!zoneRGB) {
        led_output_clear(&strip1);
        led_output_flush(&strip1);
        return;
    }

    if (macro == ZoneMacro::IDLE) {
        for (int i = 0; i < 20; i++) {
            uint8_t r = scale8(apply_dimming(zoneRGB[i * 3],     dimcurve), masterStrip);
            uint8_t g = scale8(apply_dimming(zoneRGB[i * 3 + 1], dimcurve), masterStrip);
            uint8_t b = scale8(apply_dimming(zoneRGB[i * 3 + 2], dimcurve), masterStrip);
            if (mirrorMode) {
                led_output_set_pixel(&strip1, i, r, g, b);
                led_output_set_pixel(&strip1, 39 - i, r, g, b);
            } else {
                led_output_set_pixel(&strip1, i * 2,     r, g, b);
                led_output_set_pixel(&strip1, i * 2 + 1, r, g, b);
            }
        }
        led_output_flush(&strip1);
        return;
    }

#ifdef RAVLIGHT_MODULE_EFFECTS
    if (macro == ZoneMacro::RAINBOW || macro == ZoneMacro::FIRE) {
        // Self-colored — bypass zone identity entirely, paint all 40
        // physical pixels directly (same engine as the RGBW macro).
        uint8_t effect = (macro == ZoneMacro::RAINBOW) ? EFFECT_RAINBOW : EFFECT_FIRE;
        effectsConfig.speed     = speed;
        effectsConfig.intensity = 255;
        if (tickEffectsPhase()) {
            uint8_t buf[VEYRON_NUM_PIXELS_1 * 3];
            renderEffectFrame(effect, 0, VEYRON_NUM_PIXELS_1, VEYRON_NUM_PIXELS_1, buf, 3);
            for (int i = 0; i < VEYRON_NUM_PIXELS_1; i++) {
                led_output_set_pixel(&strip1, i,
                    scale8(apply_dimming(buf[i * 3],     dimcurve), masterStrip),
                    scale8(apply_dimming(buf[i * 3 + 1], dimcurve), masterStrip),
                    scale8(apply_dimming(buf[i * 3 + 2], dimcurve), masterStrip));
            }
            led_output_flush(&strip1);
        }
        return;
    }
#endif

    uint8_t mask[20];
    renderMovementMask(macro, speed, mask, 20, s_zoneMacroPhase, s_zoneMacroLastMs);
    for (int i = 0; i < 20; i++) {
        uint8_t r = scale8(scale8(apply_dimming(zoneRGB[i * 3],     dimcurve), mask[i]), masterStrip);
        uint8_t g = scale8(scale8(apply_dimming(zoneRGB[i * 3 + 1], dimcurve), mask[i]), masterStrip);
        uint8_t b = scale8(scale8(apply_dimming(zoneRGB[i * 3 + 2], dimcurve), mask[i]), masterStrip);
        if (mirrorMode) {
            led_output_set_pixel(&strip1, i, r, g, b);
            led_output_set_pixel(&strip1, 39 - i, r, g, b);
        } else {
            led_output_set_pixel(&strip1, i * 2,     r, g, b);
            led_output_set_pixel(&strip1, i * 2 + 1, r, g, b);
        }
    }
    led_output_flush(&strip1);
}

// Renders all 40 physical pixels directly (Full Pixel + Macro) with the
// Pixel Macro applied — same mask-preserving idea as renderZoneStrip() but
// no mirror/group mapping needed, already full 1:1 resolution.
static void renderPixelStrip(const uint8_t* pixelRGB, ZoneMacro macro,
                              uint8_t speed, uint8_t masterStrip) {
    if (!pixelRGB) {
        led_output_clear(&strip1);
        led_output_flush(&strip1);
        return;
    }

    if (macro == ZoneMacro::IDLE) {
        for (int i = 0; i < VEYRON_NUM_PIXELS_1; i++) {
            led_output_set_pixel(&strip1, i,
                scale8(apply_dimming(pixelRGB[i * 3],     dimcurve), masterStrip),
                scale8(apply_dimming(pixelRGB[i * 3 + 1], dimcurve), masterStrip),
                scale8(apply_dimming(pixelRGB[i * 3 + 2], dimcurve), masterStrip));
        }
        led_output_flush(&strip1);
        return;
    }

#ifdef RAVLIGHT_MODULE_EFFECTS
    if (macro == ZoneMacro::RAINBOW || macro == ZoneMacro::FIRE) {
        uint8_t effect = (macro == ZoneMacro::RAINBOW) ? EFFECT_RAINBOW : EFFECT_FIRE;
        effectsConfig.speed     = speed;
        effectsConfig.intensity = 255;
        if (tickEffectsPhase()) {
            uint8_t buf[VEYRON_NUM_PIXELS_1 * 3];
            renderEffectFrame(effect, 0, VEYRON_NUM_PIXELS_1, VEYRON_NUM_PIXELS_1, buf, 3);
            for (int i = 0; i < VEYRON_NUM_PIXELS_1; i++) {
                led_output_set_pixel(&strip1, i,
                    scale8(apply_dimming(buf[i * 3],     dimcurve), masterStrip),
                    scale8(apply_dimming(buf[i * 3 + 1], dimcurve), masterStrip),
                    scale8(apply_dimming(buf[i * 3 + 2], dimcurve), masterStrip));
            }
            led_output_flush(&strip1);
        }
        return;
    }
#endif

    uint8_t mask[VEYRON_NUM_PIXELS_1];
    renderMovementMask(macro, speed, mask, VEYRON_NUM_PIXELS_1, s_pixelMacroPhase, s_pixelMacroLastMs);
    for (int i = 0; i < VEYRON_NUM_PIXELS_1; i++) {
        led_output_set_pixel(&strip1, i,
            scale8(scale8(apply_dimming(pixelRGB[i * 3],     dimcurve), mask[i]), masterStrip),
            scale8(scale8(apply_dimming(pixelRGB[i * 3 + 1], dimcurve), mask[i]), masterStrip),
            scale8(scale8(apply_dimming(pixelRGB[i * 3 + 2], dimcurve), mask[i]), masterStrip));
    }
    led_output_flush(&strip1);
}

// Personality 1: Full Pixel (Legacy) — frozen simple layout (128ch): RGB
// pixels + 6 accent whites + shutter strip/accent, no Master Dimmer/Macro.
// Kept stable so already-patched consoles aren't broken by the newer Rich
// tier (Personality 2).
void handleDMXPersonality1() {
    strobeRate1 = getChannelById(&veyron_patch, dmxBuffer, ID_STROBE_STRIP);
    applyStrobe(strobeRate1);
    strobeRate2 = getChannelById(&veyron_patch, dmxBuffer, ID_STROBE_ACCENT);
    applyStrobe2(strobeRate2);

    uint8_t n;
    const uint8_t* s = getChannelBlockById(&veyron_patch, dmxBuffer, ID_STRIP_PIXELS, &n);
    if (led1State && s) {
        for (int i = 0; i < VEYRON_NUM_PIXELS_1; i++) {
            led_output_set_pixel(&strip1, i,
                apply_dimming(s[i * 3],     dimcurve),
                apply_dimming(s[i * 3 + 1], dimcurve),
                apply_dimming(s[i * 3 + 2], dimcurve));
        }
    } else {
        led_output_clear(&strip1);
    }

    uint8_t a[6];
    readAccentWhites(a);
    if (led2State) {
        for (int i = 0; i < VEYRON_NUM_PIXELS_2; i++) {
            uint8_t d1 = apply_dimming(a[i * 3],     dimcurve);
            uint8_t d2 = apply_dimming(a[i * 3 + 1], dimcurve);
            uint8_t d3 = apply_dimming(a[i * 3 + 2], dimcurve);
            if (i == 0) p9813_set_pixel(&strip2, i, d2, d1, d3);
            else        p9813_set_pixel(&strip2, i, d2, d3, d1);
        }
    } else {
        p9813_clear(&strip2);
    }
    led_output_flush(&strip1);
    p9813_flush(&strip2);
}

// Personality 2: Full Pixel + Macro (133ch) — Shutter + Master Dimmer +
// Pixel Macro (preserves each of the 40 pixels' own patched color) + White
// Macro for the accent.
void handleDMXPersonality2() {
    strobeRate1 = getChannelById(&veyron_patch, dmxBuffer, ID_STROBE_STRIP);
    applyStrobe(strobeRate1);
    strobeRate2 = getChannelById(&veyron_patch, dmxBuffer, ID_STROBE_ACCENT);
    applyStrobe2(strobeRate2);
    uint8_t masterStrip  = getChannelById(&veyron_patch, dmxBuffer, ID_DIMMER_STRIP);
    uint8_t masterAccent = getChannelById(&veyron_patch, dmxBuffer, ID_DIMMER_ACCENT);
    uint8_t pixelMacroByte = getChannelById(&veyron_patch, dmxBuffer, ID_PIXEL_MACRO);
    uint8_t whiteMacroByte = getChannelById(&veyron_patch, dmxBuffer, ID_WHITE_MACRO);
    uint8_t speedByte      = getChannelById(&veyron_patch, dmxBuffer, ID_MACRO_SPEED);

    uint8_t n;
    const uint8_t* s = getChannelBlockById(&veyron_patch, dmxBuffer, ID_STRIP_PIXELS, &n);
    if (led1State) {
        renderPixelStrip(s, decodeZoneMacro(pixelMacroByte), speedByte, masterStrip);
    } else {
        led_output_clear(&strip1);
        led_output_flush(&strip1);
    }

    uint8_t a[6];
    readAccentWhites(a);
    if (led2State) {
        if (whiteMacroByte < 10) {
            for (int i = 0; i < VEYRON_NUM_PIXELS_2; i++) {
                uint8_t d1 = scale8(apply_dimming(a[i * 3],     dimcurve), masterAccent);
                uint8_t d2 = scale8(apply_dimming(a[i * 3 + 1], dimcurve), masterAccent);
                uint8_t d3 = scale8(apply_dimming(a[i * 3 + 2], dimcurve), masterAccent);
                if (i == 0) p9813_set_pixel(&strip2, i, d2, d1, d3);
                else        p9813_set_pixel(&strip2, i, d2, d3, d1);
            }
        } else {
            uint8_t w[6];
            renderWhiteMacroToAccent(whiteMacroByte, speedByte, w);
            uint8_t d0 = scale8(apply_dimming(w[0], dimcurve), masterAccent);
            uint8_t d1 = scale8(apply_dimming(w[1], dimcurve), masterAccent);
            uint8_t d2 = scale8(apply_dimming(w[2], dimcurve), masterAccent);
            uint8_t d3 = scale8(apply_dimming(w[3], dimcurve), masterAccent);
            uint8_t d4 = scale8(apply_dimming(w[4], dimcurve), masterAccent);
            uint8_t d5 = scale8(apply_dimming(w[5], dimcurve), masterAccent);
            p9813_set_pixel(&strip2, 0, d1, d0, d2);
            p9813_set_pixel(&strip2, 1, d4, d5, d3);
        }
    } else {
        p9813_clear(&strip2);
    }
    p9813_flush(&strip2);
}

// Personality 3: Full Pixel (bare, 126ch) — 40px RGB + 6 accent whites, no
// shutter/dimmer/macro, always fully lit.
void handleDMXPersonality3() {
    uint8_t n;
    const uint8_t* s = getChannelBlockById(&veyron_patch, dmxBuffer, ID_STRIP_PIXELS, &n);
    if (s) {
        for (int i = 0; i < VEYRON_NUM_PIXELS_1; i++) {
            led_output_set_pixel(&strip1, i,
                apply_dimming(s[i * 3],     dimcurve),
                apply_dimming(s[i * 3 + 1], dimcurve),
                apply_dimming(s[i * 3 + 2], dimcurve));
        }
    }
    led_output_flush(&strip1);

    uint8_t a[6];
    readAccentWhites(a);
    for (int i = 0; i < VEYRON_NUM_PIXELS_2; i++) {
        uint8_t d1 = apply_dimming(a[i * 3],     dimcurve);
        uint8_t d2 = apply_dimming(a[i * 3 + 1], dimcurve);
        uint8_t d3 = apply_dimming(a[i * 3 + 2], dimcurve);
        if (i == 0) p9813_set_pixel(&strip2, i, d2, d1, d3);
        else        p9813_set_pixel(&strip2, i, d3, d1, d2);
    }
    p9813_flush(&strip2);
}

// Personality 4: Mirror + Macro (73ch) — 20-zone mirrored control (60ch) + 6
// accent whites + shutter×2 + master dimmer×2 + Zone Macro/White Macro/Speed.
// See renderZoneStrip() for the zone-color-preserving engine.
void handleDMXPersonality4() {
    strobeRate1 = getChannelById(&veyron_patch, dmxBuffer, ID_STROBE_STRIP);
    applyStrobe(strobeRate1);
    strobeRate2 = getChannelById(&veyron_patch, dmxBuffer, ID_STROBE_ACCENT);
    applyStrobe2(strobeRate2);
    uint8_t masterStrip  = getChannelById(&veyron_patch, dmxBuffer, ID_DIMMER_STRIP);
    uint8_t masterAccent = getChannelById(&veyron_patch, dmxBuffer, ID_DIMMER_ACCENT);
    uint8_t zoneMacroByte  = getChannelById(&veyron_patch, dmxBuffer, ID_ZONE_MACRO);
    uint8_t whiteMacroByte = getChannelById(&veyron_patch, dmxBuffer, ID_WHITE_MACRO);
    uint8_t speedByte      = getChannelById(&veyron_patch, dmxBuffer, ID_MACRO_SPEED);

    uint8_t n;
    const uint8_t* mirror = getChannelBlockById(&veyron_patch, dmxBuffer, ID_STRIP_MIRROR, &n);
    if (led1State) {
        renderZoneStrip(mirror, true, decodeZoneMacro(zoneMacroByte), speedByte, masterStrip);
    } else {
        led_output_clear(&strip1);
        led_output_flush(&strip1);
    }

    uint8_t a[6];
    readAccentWhites(a);
    if (led2State) {
        if (whiteMacroByte < 10) {
            for (int i = 0; i < VEYRON_NUM_PIXELS_2; i++) {
                uint8_t d1 = scale8(apply_dimming(a[i * 3],     dimcurve), masterAccent);
                uint8_t d2 = scale8(apply_dimming(a[i * 3 + 1], dimcurve), masterAccent);
                uint8_t d3 = scale8(apply_dimming(a[i * 3 + 2], dimcurve), masterAccent);
                if (i == 0) p9813_set_pixel(&strip2, i, d2, d1, d3);
                else        p9813_set_pixel(&strip2, i, d2, d3, d1);
            }
        } else {
            uint8_t w[6];
            renderWhiteMacroToAccent(whiteMacroByte, speedByte, w);
            uint8_t d0 = scale8(apply_dimming(w[0], dimcurve), masterAccent);
            uint8_t d1 = scale8(apply_dimming(w[1], dimcurve), masterAccent);
            uint8_t d2 = scale8(apply_dimming(w[2], dimcurve), masterAccent);
            uint8_t d3 = scale8(apply_dimming(w[3], dimcurve), masterAccent);
            uint8_t d4 = scale8(apply_dimming(w[4], dimcurve), masterAccent);
            uint8_t d5 = scale8(apply_dimming(w[5], dimcurve), masterAccent);
            p9813_set_pixel(&strip2, 0, d1, d0, d2);
            p9813_set_pixel(&strip2, 1, d4, d5, d3);
        }
    } else {
        p9813_clear(&strip2);
    }
    p9813_flush(&strip2);
}

// Personality 5: Mirror (bare, 66ch) — 20-zone mirrored control (60ch) + 6
// accent whites, no shutter/dimmer/macro, always fully lit.
void handleDMXPersonality5() {
    uint8_t n;
    const uint8_t* mirror = getChannelBlockById(&veyron_patch, dmxBuffer, ID_STRIP_MIRROR, &n);
    if (mirror) {
        for (int i = 0; i < VEYRON_NUM_PIXELS_1 / 2; i++) {
            uint8_t r = apply_dimming(mirror[i * 3],     dimcurve);
            uint8_t g = apply_dimming(mirror[i * 3 + 1], dimcurve);
            uint8_t b = apply_dimming(mirror[i * 3 + 2], dimcurve);
            led_output_set_pixel(&strip1, i,      r, g, b);
            led_output_set_pixel(&strip1, 39 - i, r, g, b);
        }
    }
    led_output_flush(&strip1);

    uint8_t a[6];
    readAccentWhites(a);
    for (int i = 0; i < VEYRON_NUM_PIXELS_2; i++) {
        uint8_t d1 = apply_dimming(a[i * 3],     dimcurve);
        uint8_t d2 = apply_dimming(a[i * 3 + 1], dimcurve);
        uint8_t d3 = apply_dimming(a[i * 3 + 2], dimcurve);
        if (i == 0) p9813_set_pixel(&strip2, i, d2, d1, d3);
        else        p9813_set_pixel(&strip2, i, d2, d3, d1);
    }
    p9813_flush(&strip2);
}

// Personality 6: Grouped 2px + Macro (73ch) — same as Personality 4, grouped
// (1ch -> 2 adjacent pixels) instead of mirrored.
void handleDMXPersonality6() {
    strobeRate1 = getChannelById(&veyron_patch, dmxBuffer, ID_STROBE_STRIP);
    applyStrobe(strobeRate1);
    strobeRate2 = getChannelById(&veyron_patch, dmxBuffer, ID_STROBE_ACCENT);
    applyStrobe2(strobeRate2);
    uint8_t masterStrip  = getChannelById(&veyron_patch, dmxBuffer, ID_DIMMER_STRIP);
    uint8_t masterAccent = getChannelById(&veyron_patch, dmxBuffer, ID_DIMMER_ACCENT);
    uint8_t zoneMacroByte  = getChannelById(&veyron_patch, dmxBuffer, ID_ZONE_MACRO);
    uint8_t whiteMacroByte = getChannelById(&veyron_patch, dmxBuffer, ID_WHITE_MACRO);
    uint8_t speedByte      = getChannelById(&veyron_patch, dmxBuffer, ID_MACRO_SPEED);

    uint8_t n;
    const uint8_t* grp = getChannelBlockById(&veyron_patch, dmxBuffer, ID_STRIP_GROUP2, &n);
    if (led1State) {
        renderZoneStrip(grp, false, decodeZoneMacro(zoneMacroByte), speedByte, masterStrip);
    } else {
        led_output_clear(&strip1);
        led_output_flush(&strip1);
    }

    uint8_t a[6];
    readAccentWhites(a);
    if (led2State) {
        if (whiteMacroByte < 10) {
            for (int i = 0; i < VEYRON_NUM_PIXELS_2; i++) {
                uint8_t d1 = scale8(apply_dimming(a[i * 3],     dimcurve), masterAccent);
                uint8_t d2 = scale8(apply_dimming(a[i * 3 + 1], dimcurve), masterAccent);
                uint8_t d3 = scale8(apply_dimming(a[i * 3 + 2], dimcurve), masterAccent);
                if (i == 0) p9813_set_pixel(&strip2, i, d2, d1, d3);
                else        p9813_set_pixel(&strip2, i, d2, d3, d1);
            }
        } else {
            uint8_t w[6];
            renderWhiteMacroToAccent(whiteMacroByte, speedByte, w);
            uint8_t d0 = scale8(apply_dimming(w[0], dimcurve), masterAccent);
            uint8_t d1 = scale8(apply_dimming(w[1], dimcurve), masterAccent);
            uint8_t d2 = scale8(apply_dimming(w[2], dimcurve), masterAccent);
            uint8_t d3 = scale8(apply_dimming(w[3], dimcurve), masterAccent);
            uint8_t d4 = scale8(apply_dimming(w[4], dimcurve), masterAccent);
            uint8_t d5 = scale8(apply_dimming(w[5], dimcurve), masterAccent);
            p9813_set_pixel(&strip2, 0, d1, d0, d2);
            p9813_set_pixel(&strip2, 1, d4, d5, d3);
        }
    } else {
        p9813_clear(&strip2);
    }
    p9813_flush(&strip2);
}

// Personality 7: Grouped 2px (bare, 66ch) — no shutter/dimmer/macro, always
// fully lit.
void handleDMXPersonality7() {
    uint8_t n;
    const uint8_t* grp = getChannelBlockById(&veyron_patch, dmxBuffer, ID_STRIP_GROUP2, &n);
    if (grp) {
        for (int i = 0; i < VEYRON_NUM_PIXELS_1 / 2; i++) {
            uint8_t r = apply_dimming(grp[i * 3],     dimcurve);
            uint8_t g = apply_dimming(grp[i * 3 + 1], dimcurve);
            uint8_t b = apply_dimming(grp[i * 3 + 2], dimcurve);
            led_output_set_pixel(&strip1, i * 2,     r, g, b);
            led_output_set_pixel(&strip1, i * 2 + 1, r, g, b);
        }
    }
    led_output_flush(&strip1);

    uint8_t a[6];
    readAccentWhites(a);
    for (int i = 0; i < VEYRON_NUM_PIXELS_2; i++) {
        uint8_t d1 = apply_dimming(a[i * 3],     dimcurve);
        uint8_t d2 = apply_dimming(a[i * 3 + 1], dimcurve);
        uint8_t d3 = apply_dimming(a[i * 3 + 2], dimcurve);
        if (i == 0) p9813_set_pixel(&strip2, i, d2, d1, d3);
        else        p9813_set_pixel(&strip2, i, d2, d3, d1);
    }
    p9813_flush(&strip2);
}

// Personality 8: RGBW + Macro (10ch) — broadcast RGBW; macro idle -> manual
// broadcast (same as Personality 9 with shutter added); macro engaged -> the
// shared macro engine paints an animated pattern across all 40 physical
// pixels using Strip Color as its base color. Both scaled by the single
// Master Intensity, gated by the shutter.
void handleDMXPersonality8() {
    strobeRate1 = getChannelById(&veyron_patch, dmxBuffer, ID_STROBE_STRIP);
    applyStrobe(strobeRate1);
    strobeRate2 = getChannelById(&veyron_patch, dmxBuffer, ID_STROBE_ACCENT);
    applyStrobe2(strobeRate2);
    uint8_t masterIntensity = getChannelById(&veyron_patch, dmxBuffer, ID_MASTER_INTENSITY);

    uint8_t rgbMacroByte   = getChannelById(&veyron_patch, dmxBuffer, ID_RGB_MACRO);
    uint8_t whiteMacroByte = getChannelById(&veyron_patch, dmxBuffer, ID_WHITE_MACRO);
    uint8_t speedByte      = getChannelById(&veyron_patch, dmxBuffer, ID_MACRO_SPEED);
    uint8_t n;
    const uint8_t* cast = getChannelBlockById(&veyron_patch, dmxBuffer, ID_STRIP_CAST, &n);

    if (!led1State) {
        led_output_clear(&strip1);
        led_output_flush(&strip1);
    } else if (rgbMacroByte < 10) {
        // Idle -> manual broadcast, same as the plain RGBW+Strobe tier.
        if (cast) {
            uint8_t r = scale8(apply_dimming(cast[0], dimcurve), masterIntensity);
            uint8_t g = scale8(apply_dimming(cast[1], dimcurve), masterIntensity);
            uint8_t b = scale8(apply_dimming(cast[2], dimcurve), masterIntensity);
            for (int i = 0; i < VEYRON_NUM_PIXELS_1; i++) {
                led_output_set_pixel(&strip1, i, r, g, b);
            }
        }
        led_output_flush(&strip1);
    }
#ifdef RAVLIGHT_MODULE_EFFECTS
    else {
        uint8_t buf[VEYRON_NUM_PIXELS_1 * 3];
        if (renderRgbMacroToStrip(rgbMacroByte, cast, speedByte, buf)) {
            for (int i = 0; i < VEYRON_NUM_PIXELS_1; i++) {
                led_output_set_pixel(&strip1, i,
                    scale8(apply_dimming(buf[i * 3],     dimcurve), masterIntensity),
                    scale8(apply_dimming(buf[i * 3 + 1], dimcurve), masterIntensity),
                    scale8(apply_dimming(buf[i * 3 + 2], dimcurve), masterIntensity));
            }
            led_output_flush(&strip1);
        }
    }
#endif

    if (led2State) {
        if (whiteMacroByte < 10) {
            uint8_t d = scale8(apply_dimming(getChannelById(&veyron_patch, dmxBuffer, ID_ACCENT_WHITE), dimcurve), masterIntensity);
            for (int i = 0; i < VEYRON_NUM_PIXELS_2; i++) p9813_set_pixel(&strip2, i, d, d, d);
        } else {
            uint8_t w[6];
            renderWhiteMacroToAccent(whiteMacroByte, speedByte, w);
            uint8_t d0 = scale8(apply_dimming(w[0], dimcurve), masterIntensity);
            uint8_t d1 = scale8(apply_dimming(w[1], dimcurve), masterIntensity);
            uint8_t d2 = scale8(apply_dimming(w[2], dimcurve), masterIntensity);
            uint8_t d3 = scale8(apply_dimming(w[3], dimcurve), masterIntensity);
            uint8_t d4 = scale8(apply_dimming(w[4], dimcurve), masterIntensity);
            uint8_t d5 = scale8(apply_dimming(w[5], dimcurve), masterIntensity);
            p9813_set_pixel(&strip2, 0, d1, d0, d2);
            p9813_set_pixel(&strip2, 1, d4, d5, d3);
        }
    } else {
        p9813_clear(&strip2);
    }
    p9813_flush(&strip2);
}

// Personality 9: RGBW (bare, 4ch) — broadcast single RGB to all strip pixels
// + white accent, no shutter/dimmer/macro, always fully lit.
void handleDMXPersonality9() {
    uint8_t n;
    const uint8_t* cast = getChannelBlockById(&veyron_patch, dmxBuffer, ID_STRIP_CAST, &n);
    if (cast) {
        uint8_t r = apply_dimming(cast[0], dimcurve);
        uint8_t g = apply_dimming(cast[1], dimcurve);
        uint8_t b = apply_dimming(cast[2], dimcurve);
        for (int i = 0; i < VEYRON_NUM_PIXELS_1; i++) {
            led_output_set_pixel(&strip1, i, r, g, b);
        }
    }
    led_output_flush(&strip1);

    uint8_t d = apply_dimming(getChannelById(&veyron_patch, dmxBuffer, ID_ACCENT_WHITE), dimcurve);
    for (int i = 0; i < VEYRON_NUM_PIXELS_2; i++) {
        p9813_set_pixel(&strip2, i, d, d, d);
    }
    p9813_flush(&strip2);
}

void startHighlight() {
    if (!isHighlight) {
        isHighlight       = true;
        startTimeHighlite = currentTime;
        ESP_LOGI(TAG, "Highlight sequence started");
    }
}

// Renders one frame of the identify animation: same two-sided one-way slide
// as the CONNECTING status pattern (pixel 0→19 and mirrored 39→20, looping),
// just full-brightness white instead of dim amber and noticeably faster —
// this is a deliberate "look at me" cue, unlike the status LED below which
// is meant to stay in the background. Accent pixels breathe blue in sync.
static void renderHighlightFrame() {
    const uint16_t period = 900;
    float pos = (float)((currentTime - startTimeHighlite) % period) / (float)period;   // 0 -> 1, loops
    const int16_t half = VEYRON_NUM_PIXELS_1 / 2;                                       // 20
    int16_t left  = (int16_t)(pos * (half - 1));                                        // 0..19
    int16_t right = (VEYRON_NUM_PIXELS_1 - 1) - left;                                   // 39..20

    led_output_clear(&strip1);
    const uint8_t tailLen = 6;
    for (uint8_t k = 0; k < tailLen; k++) {
        uint8_t v = (uint8_t)(255 * (tailLen - k) / tailLen);
        int16_t lp = left  - k;
        int16_t rp = right + k;
        if (lp >= 0 && lp < VEYRON_NUM_PIXELS_1)
            led_output_set_pixel(&strip1, lp, v, v, v);
        if (rp >= 0 && rp < VEYRON_NUM_PIXELS_1)
            led_output_set_pixel(&strip1, rp, v, v, v);
    }
    led_output_flush(&strip1);

    float   phase  = (currentTime % 2000) / 2000.0f;
    uint8_t breath = (uint8_t)(127.0f * (1.0f - cosf(2.0f * (float)M_PI * phase)));
    p9813_set_pixel(&strip2, 0, 0, 0, breath);
    p9813_set_pixel(&strip2, 1, 0, 0, breath);
    p9813_flush(&strip2);
}

void higliteSequence() {
    if (!isHighlight) return;

    if (currentTime - startTimeHighlite >= VEYRON_HIGHLIGHT_DURATION) {
        isHighlight = false;
        led_output_clear(&strip1);
        led_output_flush(&strip1);
        p9813_clear(&strip2);
        p9813_flush(&strip2);
        startDMX();
        ESP_LOGI(TAG, "Highlight sequence stopped");
        return;
    }
    if (handleDMXenable) stopDMX();
    renderHighlightFrame();
}

void fixtureHighlight() { startHighlight(); }

// ---------------------------------------------------------------------------
// Status LED — reflects network link state and OTA upload progress on the
// fixture's own strip, so an operator gets boot/reconnect/flashing feedback
// without a laptop. Opt-in via veyronConfig.statusLedEnable; disabled
// fixtures never touch stopDMX() or the LED outputs from this module.
//
// Runs at STATUS_LED_BRIGHTNESS (~10% of full) — visible in a dark rig
// without reading as a "look at me" cue during a show, unlike the highlight
// sequence above which is a deliberate full-brightness identify pulse.
//
// Priority (highest wins, each is exclusive of the others):
//   1. OTA upload in progress   — steady blue progress bar + breathing accent
//   2. WiFi/ETH connecting      — slow amber comet sweep
//   3. SoftAP fallback active   — slow magenta breathing (needs setup)
//   4. Just connected           — quick green flash, then auto-clears
// Same override mechanism as highlight: stopDMX() while active, startDMX()
// once the overlay has nothing left to show, so a live DMX stream can never
// fight it for the strip.
// ---------------------------------------------------------------------------
#define STATUS_LED_BRIGHTNESS 26   // ~10% of 255
#define NET_CONNECTED_FLASH_MS 1500

typedef enum { NET_UI_IDLE, NET_UI_CONNECTING, NET_UI_CONNECTED_FLASH, NET_UI_AP } net_ui_state_t;
static net_ui_state_t netUiState = NET_UI_IDLE;
static uint32_t       netUiSince = 0;

static bool    otaActive         = false;
static uint8_t otaPercent        = 0;
static bool    statusOverlayHeld = false;   // true while we're the one holding stopDMX()

void fixtureSetNetStatus(net_status_t status) {
    switch (status) {
        case NET_STATUS_CONNECTING: netUiState = NET_UI_CONNECTING; break;
        case NET_STATUS_CONNECTED:  netUiState = NET_UI_CONNECTED_FLASH; netUiSince = now_ms(); break;
        case NET_STATUS_AP_MODE:    netUiState = NET_UI_AP; break;
    }
}

void fixtureSetOtaProgress(int16_t percent) {
    if (percent < 0) { otaActive = false; return; }
    otaActive  = true;
    otaPercent = (uint8_t)(percent > 100 ? 100 : percent);
}

// Called from network_manager.cpp's connect-wait loops (initEthernet()/
// initWiFi() block in setup(), before the main loop()/handleDMX() exist) so
// the CONNECTING comet actually animates during a cold-boot link wait
// instead of only starting once the main loop takes over.
void fixtureTickStatus() {
    currentTime = now_ms();
    tickStatusOverlay();
}

static void endStatusOverlay() {
    if (!statusOverlayHeld) return;
    statusOverlayHeld = false;
    led_output_clear(&strip1);
    led_output_flush(&strip1);
    p9813_clear(&strip2);
    p9813_flush(&strip2);
    startDMX();
}

// Renders the current status overlay if one is active. Returns true if it
// drew this frame — the caller must skip the normal DMX render entirely.
static bool tickStatusOverlay() {
    if (!veyronConfig.statusLedEnable) { endStatusOverlay(); return false; }

    // The "just connected" flash is momentary — auto-expire it back to idle.
    if (netUiState == NET_UI_CONNECTED_FLASH &&
        currentTime - netUiSince >= NET_CONNECTED_FLASH_MS) {
        netUiState = NET_UI_IDLE;
    }

    bool active = otaActive || netUiState != NET_UI_IDLE;
    if (!active) { endStatusOverlay(); return false; }

    if (!statusOverlayHeld) {
        statusOverlayHeld = true;
        if (handleDMXenable) stopDMX();
    }

    if (otaActive) {
        led_output_clear(&strip1);
        uint8_t lit = (uint8_t)((uint32_t)VEYRON_NUM_PIXELS_1 * otaPercent / 100);
        for (uint8_t i = 0; i < lit; i++)
            led_output_set_pixel(&strip1, i, 0, 0, STATUS_LED_BRIGHTNESS);
        led_output_flush(&strip1);

        float   phase  = (currentTime % 1200) / 1200.0f;
        uint8_t breath = (uint8_t)(STATUS_LED_BRIGHTNESS * (0.3f + 0.7f * fabsf(sinf((float)M_PI * phase))));
        p9813_set_pixel(&strip2, 0, 0, 0, breath);
        p9813_set_pixel(&strip2, 1, 0, 0, breath);
        p9813_flush(&strip2);
        return true;
    }

    switch (netUiState) {
        case NET_UI_CONNECTING: {
            // Two dim amber comets sliding one-way inward from both ends —
            // pixel 0→19 and mirrored 39→20 — looping back to the start
            // each cycle instead of bouncing back out. Reads as "searching"
            // without sweeping the whole bar the way the (bright white)
            // highlight comet does; amber keeps status patterns visually
            // distinct from white, which is reserved for the identify sequence.
            const uint16_t period = 1800;
            float pos = (float)(currentTime % period) / (float)period;   // 0 -> 1, loops
            const int16_t half = VEYRON_NUM_PIXELS_1 / 2;                 // 20
            int16_t left  = (int16_t)(pos * (half - 1));                  // 0..19
            int16_t right = (VEYRON_NUM_PIXELS_1 - 1) - left;             // 39..20
            led_output_clear(&strip1);
            const uint8_t tailLen = 4;
            for (uint8_t k = 0; k < tailLen; k++) {
                uint8_t v = (uint8_t)(STATUS_LED_BRIGHTNESS * (tailLen - k) / tailLen);
                int16_t lp = left  - k;
                int16_t rp = right + k;
                if (lp >= 0 && lp < VEYRON_NUM_PIXELS_1)
                    led_output_set_pixel(&strip1, lp, v, (uint8_t)(v * 0.6f), 0);
                if (rp >= 0 && rp < VEYRON_NUM_PIXELS_1)
                    led_output_set_pixel(&strip1, rp, v, (uint8_t)(v * 0.6f), 0);
            }
            led_output_flush(&strip1);
            p9813_clear(&strip2);
            p9813_flush(&strip2);
            break;
        }
        case NET_UI_AP: {
            // Sober, low-key pattern — "no uplink, needs configuring" without
            // lighting up the whole bar: only the first and last pixel of the
            // main strip breathe magenta, everything between stays off.
            float   phase  = (currentTime % 2200) / 2200.0f;
            uint8_t breath = (uint8_t)(STATUS_LED_BRIGHTNESS * (0.2f + 0.8f * fabsf(sinf((float)M_PI * phase))));
            led_output_clear(&strip1);
            led_output_set_pixel(&strip1, 0,                       breath, 0, breath);
            led_output_set_pixel(&strip1, VEYRON_NUM_PIXELS_1 - 1, breath, 0, breath);
            led_output_flush(&strip1);
            p9813_set_pixel(&strip2, 0, breath, 0, breath);
            p9813_set_pixel(&strip2, 1, breath, 0, breath);
            p9813_flush(&strip2);
            break;
        }
        case NET_UI_CONNECTED_FLASH: {
            for (int i = 0; i < VEYRON_NUM_PIXELS_1; i++)
                led_output_set_pixel(&strip1, i, 0, STATUS_LED_BRIGHTNESS, 0);
            led_output_flush(&strip1);
            p9813_set_pixel(&strip2, 0, 0, STATUS_LED_BRIGHTNESS, 0);
            p9813_set_pixel(&strip2, 1, 0, STATUS_LED_BRIGHTNESS, 0);
            p9813_flush(&strip2);
            break;
        }
        default: break;
    }
    return true;
}

#endif // RAVLIGHT_FIXTURE_VEYRON
