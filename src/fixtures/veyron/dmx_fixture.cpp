#ifdef RAVLIGHT_FIXTURE_VEYRON
#include "fixtures/veyron/dmx_fixture.h"
#include "fixtures/veyron/fixture_pins.h"
#include "fixtures/veyron/fixture_config.h"
#include "fixtures/veyron/fixture_ids.h"
#include "fixtures/veyron/personalities.h"
#include "core/dmx_patch.h"
#include "dmx_manager.h"
//#define FASTLED_EXPERIMENTAL_ESP32_RGBW_ENABLED 1
#include <FastLED.h>
#include "FastLED_RGBW.h"

enum State { IDLE, PIX1, PIX2, PIX3, OFF };
State currentState = IDLE;
unsigned long lastTimeHighlight = 0;
unsigned long startTimeHighlite = 0;
bool isHighlight = false;

bool led1State = false;
bool led2State = false;

extern uint8_t dmxBuffer[];
bool handleDMXenable = true;

//CRGBW leds1[VEYRON_NUM_PIXELS_1];
//CRGB *ledsRGB = (CRGB *) &leds1[0];
CRGB leds1[VEYRON_NUM_PIXELS_1];
CRGB leds2[VEYRON_NUM_PIXELS_2];

uint8_t strobeRate1 = 0;
uint8_t strobeRate2 = 0;
uint16_t dimcurve;

static uint32_t lastTime1 = 0;
static uint32_t lastTime2 = 0;
uint32_t currentTime = 0;

// Patch state — initialised in initFixture()
static patch_state_t veyron_patch;

void initFixture() {
    veyron_patch.table           = VEYRON_PERSONALITIES;
    veyron_patch.personality_idx = (uint8_t)(dmxConfig.selectedPersonality - 1); // 1-based → 0-based
    veyron_patch.universe        = (uint8_t)dmxConfig.startUniverse;
    veyron_patch.section_start[VEYRON_SEC_STRIP]  = (uint16_t)dmxConfig.RGBWstartAddress;
    veyron_patch.section_start[VEYRON_SEC_ACCENT] = (uint16_t)dmxConfig.WhStartAddress;
    veyron_patch.section_start[VEYRON_SEC_STROBE] = (uint16_t)dmxConfig.strobeStartAddress;

    dimcurve = setConfig.DimCurves;

    //FastLED.addLeds<WS2811, HW_PIN_STRIP, RGB>(ledsRGB, getRGBWsize(VEYRON_NUM_PIXELS_1));
    FastLED.addLeds<WS2811, HW_PIN_STRIP, RGB>(leds1, VEYRON_NUM_PIXELS_1);
    FastLED.addLeds<P9813, HW_PIN_P9813_DATA, HW_PIN_P9813_CLK, RGB, DATA_RATE_MHZ(10)>(leds2, VEYRON_NUM_PIXELS_2);
    FastLED.clear();
    FastLED.show();
}

void setPersonality(FixturePersonality personality) {
    dmxConfig.selectedPersonality = personality;
    veyron_patch.personality_idx  = (uint8_t)(personality - 1);
    Serial.printf("[FIXTURE] DMX personality set: %d\n", dmxConfig.selectedPersonality);
}

void setDimCurve(uint16_t curve) {
    dimcurve = curve;
    setConfig.DimCurves = curve;
}

void setFixtureAddresses(int rgbwStart, int whStart, int strobeStart) {
    veyron_patch.section_start[VEYRON_SEC_STRIP]  = (uint16_t)rgbwStart;
    veyron_patch.section_start[VEYRON_SEC_ACCENT] = (uint16_t)whStart;
    veyron_patch.section_start[VEYRON_SEC_STROBE] = (uint16_t)strobeStart;
    dmxConfig.RGBWstartAddress   = rgbwStart;
    dmxConfig.WhStartAddress     = whStart;
    dmxConfig.strobeStartAddress = strobeStart;
}

void stopDMX() {
    handleDMXenable = false;
    Serial.println("[DMX] Handler stopped");
    FastLED.clear(true);
}

void startDMX() {
    handleDMXenable = true;
    Serial.println("[DMX] Handler started");
    FastLED.clear(true);
}

void handleDMX() {
    if (handleDMXenable) {
        switch (dmxConfig.selectedPersonality) {
            case PERSONALITY_1: handleDMXPersonality1(); break;
            case PERSONALITY_2: handleDMXPersonality2(); break;
            case PERSONALITY_3: handleDMXPersonality3(); break;
            case PERSONALITY_4: handleDMXPersonality4(); break;
            case PERSONALITY_5: handleDMXPersonality5(); break;
            default:
                Serial.println("[FIXTURE] Unknown DMX personality");
                break;
        }
    }
    higliteSequence();
    currentTime = millis();
}

uint8_t apply_dimming(uint8_t color_value, uint16_t curve) {
    float normalized = color_value / 255.0;
    float scaled_value;
    switch (curve) {
        case LINEAR:         scaled_value = normalized; break;
        case SQUARE:         scaled_value = pow(normalized, 2); break;
        case INVERSE_SQUARE: scaled_value = sqrt(normalized); break;
        case S_CURVE:        scaled_value = 1 / (1 + exp(-12 * (normalized - 0.5))); break;
        default:             scaled_value = normalized; break;
    }
    return (uint8_t)(scaled_value * 255);
}

// Personality 1: 40px RGB (120ch) + 2px accent RGB (6ch) + 2 strobe channels
void handleDMXPersonality1() {
    strobeRate1 = getChannelById(&veyron_patch, dmxBuffer, ID_STROBE_STRIP);
    applyStrobe(strobeRate1);
    strobeRate2 = getChannelById(&veyron_patch, dmxBuffer, ID_STROBE_ACCENT);
    applyStrobe2(strobeRate2);

    uint8_t n;
    const uint8_t* strip = getChannelBlockById(&veyron_patch, dmxBuffer, ID_STRIP_PIXELS, &n);
    if (led1State && strip) {
        for (int i = 0; i < VEYRON_NUM_PIXELS_1; i++) {
            uint8_t r = strip[i * 3];
            uint8_t g = strip[i * 3 + 1];
            uint8_t b = strip[i * 3 + 2];
            leds1[i] = CRGB(apply_dimming(r, dimcurve), apply_dimming(g, dimcurve), apply_dimming(b, dimcurve));
        }
    } else {
        for (int i = 0; i < VEYRON_NUM_PIXELS_1; i++) leds1[i] = CRGB(0, 0, 0);
    }

    const uint8_t* acc = getChannelBlockById(&veyron_patch, dmxBuffer, ID_ACCENT_PIXELS, &n);
    if (led2State && acc) {
        for (int i = 0; i < VEYRON_NUM_PIXELS_2; i++) {
            uint8_t d1 = apply_dimming(acc[i * 3],     dimcurve);
            uint8_t d2 = apply_dimming(acc[i * 3 + 1], dimcurve);
            uint8_t d3 = apply_dimming(acc[i * 3 + 2], dimcurve);
            leds2[i] = (i == 0) ? CRGB(d2, d1, d3) : CRGB(d2, d3, d1);
        }
    } else {
        for (int i = 0; i < VEYRON_NUM_PIXELS_2; i++) leds2[i] = CRGB(0, 0, 0);
    }
    FastLED.show();
}

// Personality 2: 40px RGB (120ch) + 2px accent RGB (6ch), no strobe
void handleDMXPersonality2() {
    uint8_t n;
    const uint8_t* strip = getChannelBlockById(&veyron_patch, dmxBuffer, ID_STRIP_PIXELS, &n);
    if (strip) {
        for (int i = 0; i < VEYRON_NUM_PIXELS_1; i++) {
            uint8_t r = strip[i * 3];
            uint8_t g = strip[i * 3 + 1];
            uint8_t b = strip[i * 3 + 2];
            leds1[i] = CRGB(apply_dimming(r, dimcurve), apply_dimming(g, dimcurve), apply_dimming(b, dimcurve));
        }
    }
    const uint8_t* acc = getChannelBlockById(&veyron_patch, dmxBuffer, ID_ACCENT_PIXELS, &n);
    if (acc) {
        for (int i = 0; i < VEYRON_NUM_PIXELS_2; i++) {
            uint8_t d1 = apply_dimming(acc[i * 3],     dimcurve);
            uint8_t d2 = apply_dimming(acc[i * 3 + 1], dimcurve);
            uint8_t d3 = apply_dimming(acc[i * 3 + 2], dimcurve);
            leds2[i] = (i == 0) ? CRGB(d2, d1, d3) : CRGB(d3, d1, d2);
        }
    }
    FastLED.show();
}

// Personality 3: single RGB color broadcast to all pixels + accent + strobe
void handleDMXPersonality3() {
    strobeRate1 = getChannelById(&veyron_patch, dmxBuffer, ID_STROBE_STRIP);
    applyStrobe(strobeRate1);
    strobeRate2 = getChannelById(&veyron_patch, dmxBuffer, ID_STROBE_ACCENT);
    applyStrobe2(strobeRate2);

    uint8_t n;
    const uint8_t* cast = getChannelBlockById(&veyron_patch, dmxBuffer, ID_STRIP_CAST, &n);
    if (led1State && cast) {
        CRGB col(apply_dimming(cast[0], dimcurve),
                 apply_dimming(cast[1], dimcurve),
                 apply_dimming(cast[2], dimcurve));
        for (int i = 0; i < VEYRON_NUM_PIXELS_1; i++) leds1[i] = col;
    } else {
        for (int i = 0; i < VEYRON_NUM_PIXELS_1; i++) leds1[i] = CRGB(0, 0, 0);
    }

    if (led2State) {
        uint8_t d = apply_dimming(getChannelById(&veyron_patch, dmxBuffer, ID_ACCENT_WHITE), dimcurve);
        for (int i = 0; i < VEYRON_NUM_PIXELS_2; i++) leds2[i] = CRGB(d, d, d);
    } else {
        for (int i = 0; i < VEYRON_NUM_PIXELS_2; i++) leds2[i] = CRGB(0, 0, 0);
    }
    FastLED.show();
}

// Personality 4: mirror 20px RGB (60ch) + 2px accent + strobe
void handleDMXPersonality4() {
    strobeRate1 = getChannelById(&veyron_patch, dmxBuffer, ID_STROBE_STRIP);
    applyStrobe(strobeRate1);
    strobeRate2 = getChannelById(&veyron_patch, dmxBuffer, ID_STROBE_ACCENT);
    applyStrobe2(strobeRate2);

    uint8_t n;
    const uint8_t* mirror = getChannelBlockById(&veyron_patch, dmxBuffer, ID_STRIP_MIRROR, &n);
    if (led1State && mirror) {
        for (int i = 0; i < VEYRON_NUM_PIXELS_1 / 2; i++) {
            CRGB col(apply_dimming(mirror[i * 3],     dimcurve),
                     apply_dimming(mirror[i * 3 + 1], dimcurve),
                     apply_dimming(mirror[i * 3 + 2], dimcurve));
            leds1[i]        = col;
            leds1[39 - i]   = col;
        }
    } else {
        for (int i = 0; i < VEYRON_NUM_PIXELS_1; i++) leds1[i] = CRGB(0, 0, 0);
    }

    const uint8_t* acc = getChannelBlockById(&veyron_patch, dmxBuffer, ID_ACCENT_PIXELS, &n);
    if (led2State && acc) {
        for (int i = 0; i < VEYRON_NUM_PIXELS_2; i++) {
            uint8_t d1 = apply_dimming(acc[i * 3],     dimcurve);
            uint8_t d2 = apply_dimming(acc[i * 3 + 1], dimcurve);
            uint8_t d3 = apply_dimming(acc[i * 3 + 2], dimcurve);
            leds2[i] = (i == 0) ? CRGB(d2, d1, d3) : CRGB(d2, d3, d1);
        }
    } else {
        for (int i = 0; i < VEYRON_NUM_PIXELS_2; i++) leds2[i] = CRGB(0, 0, 0);
    }
    FastLED.show();
}

// Personality 5: grouped 2px per DMX channel (20ch -> 40px) + 2px accent + strobe
void handleDMXPersonality5() {
    strobeRate1 = getChannelById(&veyron_patch, dmxBuffer, ID_STROBE_STRIP);
    applyStrobe(strobeRate1);
    strobeRate2 = getChannelById(&veyron_patch, dmxBuffer, ID_STROBE_ACCENT);
    applyStrobe2(strobeRate2);

    uint8_t n;
    const uint8_t* grp = getChannelBlockById(&veyron_patch, dmxBuffer, ID_STRIP_GROUP2, &n);
    if (led1State && grp) {
        const int dmxGroups = VEYRON_NUM_PIXELS_1 / 2;
        for (int i = 0; i < dmxGroups; i++) {
            CRGB col(apply_dimming(grp[i * 3],     dimcurve),
                     apply_dimming(grp[i * 3 + 1], dimcurve),
                     apply_dimming(grp[i * 3 + 2], dimcurve));
            leds1[i * 2]     = col;
            leds1[i * 2 + 1] = col;
        }
    } else {
        for (int i = 0; i < VEYRON_NUM_PIXELS_1; i++) leds1[i] = CRGB(0, 0, 0);
    }

    const uint8_t* acc = getChannelBlockById(&veyron_patch, dmxBuffer, ID_ACCENT_PIXELS, &n);
    if (led2State && acc) {
        for (int i = 0; i < VEYRON_NUM_PIXELS_2; i++) {
            uint8_t d1 = apply_dimming(acc[i * 3],     dimcurve);
            uint8_t d2 = apply_dimming(acc[i * 3 + 1], dimcurve);
            uint8_t d3 = apply_dimming(acc[i * 3 + 2], dimcurve);
            leds2[i] = (i == 0) ? CRGB(d2, d1, d3) : CRGB(d2, d3, d1);
        }
    } else {
        for (int i = 0; i < VEYRON_NUM_PIXELS_2; i++) leds2[i] = CRGB(0, 0, 0);
    }
    FastLED.show();
}

void applyStrobe(uint8_t strobeRate) {
    if (strobeRate == 0) {
        led1State = true;
    } else {
        float strobecurve = VEYRON_STROBE_CURVE_A * pow(strobeRate, VEYRON_STROBE_CURVE_B);
        float interval = map(strobecurve, 1, 255, VEYRON_STROBE_RATE_MIN, VEYRON_STROBE_RATE_MAX);
        if (lastTime1 + interval > currentTime) {
            led1State = false;
        } else if (lastTime1 + interval + VEYRON_STROBE_DURATION < currentTime) {
            lastTime1 = currentTime;
        } else {
            led1State = true;
        }
    }
}

void applyStrobe2(uint8_t strobeRate) {
    if (strobeRate == 0) {
        led2State = true;
    } else {
        float strobecurve = VEYRON_STROBE_CURVE_A * pow(strobeRate, VEYRON_STROBE_CURVE_B);
        float interval = map(strobecurve, 1, 255, VEYRON_STROBE_RATE_MIN, VEYRON_STROBE_RATE_MAX);
        if (lastTime2 + interval > currentTime) {
            led2State = false;
        } else if (lastTime2 + interval + VEYRON_STROBE_DURATION < currentTime) {
            lastTime2 = currentTime;
        } else {
            led2State = true;
        }
    }
}

void startHighlight() {
    if (!isHighlight) {
        isHighlight = true;
        startTimeHighlite = currentTime;
        currentState = PIX1;
        Serial.println("[FIXTURE] Highlight sequence started");
    }
}

void higliteSequence() {
    if (!isHighlight) return;

    if (currentTime - startTimeHighlite >= VEYRON_HIGHLIGHT_DURATION) {
        isHighlight = false;
        FastLED.clear();
        FastLED.show();
        currentState = IDLE;
        startDMX();
        Serial.println("[FIXTURE] Highlight sequence stopped");
        return;
    }
    if (handleDMXenable) stopDMX();

    if (currentTime - lastTimeHighlight >= VEYRON_STEP_HIGHLIGHT) {
        lastTimeHighlight = currentTime;
        switch (currentState) {
            case PIX1: leds2[0] = CRGB(0,0,255); leds2[1] = CRGB(255,0,0); currentState = PIX2; break;
            case PIX2: leds2[0] = CRGB(0,255,0); leds2[1] = CRGB(0,255,0); currentState = PIX3; break;
            case PIX3: leds2[0] = CRGB(255,0,0); leds2[1] = CRGB(0,0,255); currentState = PIX1; break;
            case IDLE:
            case OFF:
            default: return;
        }
        FastLED.show();
    }
}
#endif // RAVLIGHT_FIXTURE_VEYRON
