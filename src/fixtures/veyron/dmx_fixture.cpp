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

enum State { IDLE, PIX1, PIX2, PIX3, OFF };
static State    currentState    = IDLE;
static uint32_t lastTimeHighlight  = 0;
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

extern uint8_t dmxBuffer[];

void initFixture() {
    veyron_patch.table           = VEYRON_PERSONALITIES;
    veyron_patch.personality_idx = (uint8_t)(veyronConfig.personality - 1);
    veyron_patch.universe        = (uint8_t)dmxConfig.startUniverse;
    veyron_patch.section_start[VEYRON_SEC_STRIP]  = veyronConfig.rgbwStart;
    veyron_patch.section_start[VEYRON_SEC_ACCENT] = veyronConfig.whiteStart;
    veyron_patch.section_start[VEYRON_SEC_STROBE] = veyronConfig.strobeStart;

    dimcurve = veyronConfig.DimCurves;

    led_output_init(&strip1, HW_LED_OUTPUT_PINS[0], VEYRON_NUM_PIXELS_1, RMT_CHANNEL_0, 4, 3);
    p9813_init(&strip2, HW_PIN_P9813_DATA, HW_PIN_P9813_CLK, VEYRON_NUM_PIXELS_2);
    led_output_clear(&strip1);
    led_output_flush(&strip1);
    p9813_clear(&strip2);
    p9813_flush(&strip2);
}

void setPersonality(FixturePersonality personality) {
    veyronConfig.personality     = personality;
    veyron_patch.personality_idx = (uint8_t)(personality - 1);
    ESP_LOGI(TAG, "DMX personality set: %d", personality);
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
    switch (veyronConfig.personality) {
        case PERSONALITY_1: handleDMXPersonality1(); break;
        case PERSONALITY_2: handleDMXPersonality2(); break;
        case PERSONALITY_3: handleDMXPersonality3(); break;
        case PERSONALITY_4: handleDMXPersonality4(); break;
        case PERSONALITY_5: handleDMXPersonality5(); break;
        default: ESP_LOGW(TAG, "Unknown DMX personality"); break;
    }
    higliteSequence();
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
        currentState      = PIX1;
        ESP_LOGI(TAG, "Highlight sequence started");
    }
}

void higliteSequence() {
    if (!isHighlight) return;

    if (currentTime - startTimeHighlite >= VEYRON_HIGHLIGHT_DURATION) {
        isHighlight  = false;
        currentState = IDLE;
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
        switch (currentState) {
            case PIX1:
                p9813_set_pixel(&strip2, 0,   0,   0, 255);  // blue
                p9813_set_pixel(&strip2, 1, 255,   0,   0);  // red
                currentState = PIX2; break;
            case PIX2:
                p9813_set_pixel(&strip2, 0,   0, 255,   0);  // green
                p9813_set_pixel(&strip2, 1,   0, 255,   0);
                currentState = PIX3; break;
            case PIX3:
                p9813_set_pixel(&strip2, 0, 255,   0,   0);  // red
                p9813_set_pixel(&strip2, 1,   0,   0, 255);  // blue
                currentState = PIX1; break;
            default: return;
        }
        p9813_flush(&strip2);
    }
}
#endif // RAVLIGHT_FIXTURE_VEYRON
