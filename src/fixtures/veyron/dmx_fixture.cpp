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
#include "driver/rmt.h"
#include <math.h>

static const char* TAG = "FIXTURE";

static inline uint32_t now_ms() {
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static inline float fmap(float x, float in_min, float in_max, float out_min, float out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

static bool tickStatusOverlay();   // status LED module, defined below

static uint32_t lastTimeHighlight  = 0;
static uint32_t startTimeHighlite  = 0;
static bool     isHighlight     = false;
static int8_t   cometPos        = 0;
static int8_t   cometDir        = 1;

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
    veyron_patch.section_start[VEYRON_SEC_STROBE] = veyronConfig.strobeStart;

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
    veyron_patch.section_start[VEYRON_SEC_STROBE] = veyronConfig.strobeStart;
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

// Recompute accent/strobe section addresses from the CURRENT personality's
// own channel widths, anchored at the strip's start address (rgbwStart,
// left untouched — it's the one address the operator/RDM actually sets).
// Needed because each personality has a completely different footprint per
// section (Personality 1: strip 120ch+accent 6ch+strobe 2ch vs Personality
// 3: cast 3ch+white 1ch+strobe 2ch) — carrying over the OLD personality's
// absolute addresses put accent/strobe channels outside the new, much
// smaller footprint (e.g. stuck at 121/127 for a 6-channel personality that
// should have them at 4/5).
static void relayoutSectionAddresses() {
    const personality_t& pers = VEYRON_PERSONALITIES[veyron_patch.personality_idx];
    uint16_t stripWidth = 0, accentWidth = 0;
    for (uint8_t i = 0; i < pers.n_channels; i++) {
        const dmx_channel_t& ch = pers.channels[i];
        if (ch.section == VEYRON_SEC_STRIP)  stripWidth  += ch.count;
        if (ch.section == VEYRON_SEC_ACCENT) accentWidth += ch.count;
    }
    uint16_t rgbw   = veyron_patch.section_start[VEYRON_SEC_STRIP];
    uint16_t accent = rgbw + stripWidth;
    uint16_t strobe = accent + accentWidth;
    setFixtureAddresses(rgbw, accent, strobe);
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

void setFixtureAddresses(int rgbwStart, int whStart, int strobeStart) {
    veyron_patch.section_start[VEYRON_SEC_STRIP]  = (uint16_t)rgbwStart;
    veyron_patch.section_start[VEYRON_SEC_ACCENT] = (uint16_t)whStart;
    veyron_patch.section_start[VEYRON_SEC_STROBE] = (uint16_t)strobeStart;
    veyronConfig.rgbwStart   = (uint16_t)rgbwStart;
    veyronConfig.whiteStart  = (uint16_t)whStart;
    veyronConfig.strobeStart = (uint16_t)strobeStart;
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
            default: ESP_LOGW(TAG, "Unknown DMX personality"); break;
        }
    }
    currentTime = now_ms();
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

// Personality 1: 40px RGB (120ch) + 2px accent RGB (6ch) + 2 strobe channels
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

    const uint8_t* a = getChannelBlockById(&veyron_patch, dmxBuffer, ID_ACCENT_PIXELS, &n);
    if (led2State && a) {
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

// Personality 2: 40px RGB (120ch) + 2px accent RGB (6ch), no strobe
void handleDMXPersonality2() {
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

    const uint8_t* a = getChannelBlockById(&veyron_patch, dmxBuffer, ID_ACCENT_PIXELS, &n);
    if (a) {
        for (int i = 0; i < VEYRON_NUM_PIXELS_2; i++) {
            uint8_t d1 = apply_dimming(a[i * 3],     dimcurve);
            uint8_t d2 = apply_dimming(a[i * 3 + 1], dimcurve);
            uint8_t d3 = apply_dimming(a[i * 3 + 2], dimcurve);
            if (i == 0) p9813_set_pixel(&strip2, i, d2, d1, d3);
            else        p9813_set_pixel(&strip2, i, d3, d1, d2);
        }
    }
    led_output_flush(&strip1);
    p9813_flush(&strip2);
}

// Personality 3: broadcast single RGB to all strip pixels + white accent + strobe
void handleDMXPersonality3() {
    strobeRate1 = getChannelById(&veyron_patch, dmxBuffer, ID_STROBE_STRIP);
    applyStrobe(strobeRate1);
    strobeRate2 = getChannelById(&veyron_patch, dmxBuffer, ID_STROBE_ACCENT);
    applyStrobe2(strobeRate2);

    uint8_t n;
    const uint8_t* cast = getChannelBlockById(&veyron_patch, dmxBuffer, ID_STRIP_CAST, &n);
    if (led1State && cast) {
        uint8_t r = apply_dimming(cast[0], dimcurve);
        uint8_t g = apply_dimming(cast[1], dimcurve);
        uint8_t b = apply_dimming(cast[2], dimcurve);
        for (int i = 0; i < VEYRON_NUM_PIXELS_1; i++) {
            led_output_set_pixel(&strip1, i, r, g, b);
        }
    } else {
        led_output_clear(&strip1);
    }

    if (led2State) {
        uint8_t d = apply_dimming(getChannelById(&veyron_patch, dmxBuffer, ID_ACCENT_WHITE), dimcurve);
        for (int i = 0; i < VEYRON_NUM_PIXELS_2; i++) {
            p9813_set_pixel(&strip2, i, d, d, d);
        }
    } else {
        p9813_clear(&strip2);
    }
    led_output_flush(&strip1);
    p9813_flush(&strip2);
}

// Personality 4: mirror 20px RGB (60ch) → 40px + 2px accent + strobe
void handleDMXPersonality4() {
    strobeRate1 = getChannelById(&veyron_patch, dmxBuffer, ID_STROBE_STRIP);
    applyStrobe(strobeRate1);
    strobeRate2 = getChannelById(&veyron_patch, dmxBuffer, ID_STROBE_ACCENT);
    applyStrobe2(strobeRate2);

    uint8_t n;
    const uint8_t* mirror = getChannelBlockById(&veyron_patch, dmxBuffer, ID_STRIP_MIRROR, &n);
    if (led1State && mirror) {
        for (int i = 0; i < VEYRON_NUM_PIXELS_1 / 2; i++) {
            uint8_t r = apply_dimming(mirror[i * 3],     dimcurve);
            uint8_t g = apply_dimming(mirror[i * 3 + 1], dimcurve);
            uint8_t b = apply_dimming(mirror[i * 3 + 2], dimcurve);
            led_output_set_pixel(&strip1, i,      r, g, b);
            led_output_set_pixel(&strip1, 39 - i, r, g, b);
        }
    } else {
        led_output_clear(&strip1);
    }

    const uint8_t* a = getChannelBlockById(&veyron_patch, dmxBuffer, ID_ACCENT_PIXELS, &n);
    if (led2State && a) {
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

// Personality 5: grouped 2px per DMX triplet (20×3ch → 40px) + 2px accent + strobe
void handleDMXPersonality5() {
    strobeRate1 = getChannelById(&veyron_patch, dmxBuffer, ID_STROBE_STRIP);
    applyStrobe(strobeRate1);
    strobeRate2 = getChannelById(&veyron_patch, dmxBuffer, ID_STROBE_ACCENT);
    applyStrobe2(strobeRate2);

    uint8_t n;
    const uint8_t* grp = getChannelBlockById(&veyron_patch, dmxBuffer, ID_STRIP_GROUP2, &n);
    if (led1State && grp) {
        for (int i = 0; i < VEYRON_NUM_PIXELS_1 / 2; i++) {
            uint8_t r = apply_dimming(grp[i * 3],     dimcurve);
            uint8_t g = apply_dimming(grp[i * 3 + 1], dimcurve);
            uint8_t b = apply_dimming(grp[i * 3 + 2], dimcurve);
            led_output_set_pixel(&strip1, i * 2,     r, g, b);
            led_output_set_pixel(&strip1, i * 2 + 1, r, g, b);
        }
    } else {
        led_output_clear(&strip1);
    }

    const uint8_t* a = getChannelBlockById(&veyron_patch, dmxBuffer, ID_ACCENT_PIXELS, &n);
    if (led2State && a) {
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

void applyStrobe(uint8_t strobeRate) {
    if (strobeRate == 0) {
        led1State = true;
        return;
    }
    float    strobecurve = VEYRON_STROBE_CURVE_A * powf(strobeRate, VEYRON_STROBE_CURVE_B);
    uint32_t interval    = (uint32_t)fmap(strobecurve, 1.0f, 255.0f,
                                          (float)VEYRON_STROBE_RATE_MIN,
                                          (float)VEYRON_STROBE_RATE_MAX);
    if (lastTime1 + interval > currentTime) {
        led1State = false;
    } else if (lastTime1 + interval + VEYRON_STROBE_DURATION < currentTime) {
        lastTime1 = currentTime;
    } else {
        led1State = true;
    }
}

void applyStrobe2(uint8_t strobeRate) {
    if (strobeRate == 0) {
        led2State = true;
        return;
    }
    float    strobecurve = VEYRON_STROBE_CURVE_A * powf(strobeRate, VEYRON_STROBE_CURVE_B);
    uint32_t interval    = (uint32_t)fmap(strobecurve, 1.0f, 255.0f,
                                          (float)VEYRON_STROBE_RATE_MIN,
                                          (float)VEYRON_STROBE_RATE_MAX);
    if (lastTime2 + interval > currentTime) {
        led2State = false;
    } else if (lastTime2 + interval + VEYRON_STROBE_DURATION < currentTime) {
        lastTime2 = currentTime;
    } else {
        led2State = true;
    }
}

void startHighlight() {
    if (!isHighlight) {
        isHighlight       = true;
        startTimeHighlite = currentTime;
        cometPos          = 0;
        cometDir          = 1;
        ESP_LOGI(TAG, "Highlight sequence started");
    }
}

// Renders one frame of the identify animation: a fading white comet running
// back and forth across the main 40 px strip, with the 2 accent pixels
// breathing blue in sync — much easier to spot across a rig at a glance than
// the previous flat 3-color cycle, and still reads clearly at full brightness
// (unlike the status LED below, this is a deliberate "look at me" cue).
static void renderHighlightFrame() {
    led_output_clear(&strip1);
    const uint8_t tailLen = 6;
    for (uint8_t t = 0; t < tailLen; t++) {
        int16_t p = cometPos - cometDir * t;
        if (p < 0 || p >= VEYRON_NUM_PIXELS_1) continue;
        uint8_t v = (uint8_t)(255 * (tailLen - t) / tailLen);
        led_output_set_pixel(&strip1, p, v, v, v);
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

    if (currentTime - lastTimeHighlight >= VEYRON_STEP_HIGHLIGHT) {
        lastTimeHighlight = currentTime;
        cometPos += cometDir;
        if (cometPos >= VEYRON_NUM_PIXELS_1 - 1) { cometPos = VEYRON_NUM_PIXELS_1 - 1; cometDir = -1; }
        else if (cometPos <= 0)                  { cometPos = 0;                       cometDir = 1;  }
    }
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
