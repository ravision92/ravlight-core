#ifdef RAVLIGHT_FIXTURE_VEYRON
#include "fixtures/veyron/dmx_fixture.h"
#include "fixtures/veyron/fixture_pins.h"
#include "fixtures/veyron/fixture_config.h"
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
uint8_t strobeRateMax = 40;
uint16_t strobeRateMin = 2500;
uint32_t currentTime = 0;
static uint32_t lastTime1 = 0;
static uint32_t lastTime2 = 0;

int RGBWstartAddress;
int WhStartAddress;
int strobeChannel1;
int strobeChannel2;
uint16_t dimcurve;

void initFixture() {
    RGBWstartAddress = dmxConfig.RGBWstartAddress;
    WhStartAddress   = dmxConfig.WhStartAddress;
    strobeChannel1   = dmxConfig.strobeStartAddress;
    strobeChannel2   = strobeChannel1 + 1;
    dimcurve         = setConfig.DimCurves;

    //FastLED.addLeds<WS2811, HW_PIN_STRIP, RGB>(ledsRGB, getRGBWsize(VEYRON_NUM_PIXELS_1));
    FastLED.addLeds<WS2811, HW_PIN_STRIP, RGB>(leds1, VEYRON_NUM_PIXELS_1);
    FastLED.addLeds<P9813, HW_PIN_P9813_DATA, HW_PIN_P9813_CLK, RGB, DATA_RATE_MHZ(10)>(leds2, VEYRON_NUM_PIXELS_2);
    FastLED.clear();
    FastLED.show();
}

void setPersonality(FixturePersonality personality) {
    dmxConfig.selectedPersonality = personality;
    Serial.printf("[FIXTURE] DMX personality set: %d\n", dmxConfig.selectedPersonality);
}

void setDimCurve(uint16_t curve) {
    dimcurve = curve;
    setConfig.DimCurves = curve;
}

void setFixtureAddresses(int rgbwStart, int whStart, int strobeStart) {
    RGBWstartAddress = rgbwStart;
    WhStartAddress   = whStart;
    strobeChannel1   = strobeStart;
    strobeChannel2   = strobeStart + 1;
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
  strobeRate1 = dmxBuffer[strobeChannel1];
  applyStrobe(strobeRate1);
  strobeRate2 = dmxBuffer[strobeChannel2];
  applyStrobe2(strobeRate2);

  if (led1State) {
    for (int i = 0; i < VEYRON_NUM_PIXELS_1; i++) {
      uint8_t r = dmxBuffer[RGBWstartAddress + i * 3];
      uint8_t g = dmxBuffer[RGBWstartAddress + i * 3 + 1];
      uint8_t b = dmxBuffer[RGBWstartAddress + i * 3 + 2];
      leds1[i] = CRGB(apply_dimming(r, dimcurve), apply_dimming(g, dimcurve), apply_dimming(b, dimcurve));
    }
  } else {
    for (int i = 0; i < VEYRON_NUM_PIXELS_1; i++) leds1[i] = CRGB(0, 0, 0);
  }

  if (led2State) {
    for (int i = 0; i < VEYRON_NUM_PIXELS_2; i++) {
      uint8_t s1 = dmxBuffer[WhStartAddress + i * 3];
      uint8_t s2 = dmxBuffer[WhStartAddress + i * 3 + 1];
      uint8_t s3 = dmxBuffer[WhStartAddress + i * 3 + 2];
      uint8_t d1 = apply_dimming(s1, dimcurve);
      uint8_t d2 = apply_dimming(s2, dimcurve);
      uint8_t d3 = apply_dimming(s3, dimcurve);
      leds2[i] = (i == 0) ? CRGB(d2, d1, d3) : CRGB(d2, d3, d1);
    }
  } else {
    for (int i = 0; i < VEYRON_NUM_PIXELS_2; i++) leds2[i] = CRGB(0, 0, 0);
  }
  FastLED.show();
}

// Personality 2: 40px RGB (120ch) + 2px accent RGB (6ch), no strobe
void handleDMXPersonality2() {
  for (int i = 0; i < VEYRON_NUM_PIXELS_1; i++) {
    uint8_t r = dmxBuffer[RGBWstartAddress + i * 3];
    uint8_t g = dmxBuffer[RGBWstartAddress + i * 3 + 1];
    uint8_t b = dmxBuffer[RGBWstartAddress + i * 3 + 2];
    leds1[i] = CRGB(apply_dimming(r, dimcurve), apply_dimming(g, dimcurve), apply_dimming(b, dimcurve));
  }
  for (int i = 0; i < VEYRON_NUM_PIXELS_2; i++) {
    uint8_t s1 = dmxBuffer[WhStartAddress + i * 3];
    uint8_t s2 = dmxBuffer[WhStartAddress + i * 3 + 1];
    uint8_t s3 = dmxBuffer[WhStartAddress + i * 3 + 2];
    uint8_t d1 = apply_dimming(s1, dimcurve);
    uint8_t d2 = apply_dimming(s2, dimcurve);
    uint8_t d3 = apply_dimming(s3, dimcurve);
    leds2[i] = (i == 0) ? CRGB(d2, d1, d3) : CRGB(d3, d1, d2);
  }
  FastLED.show();
}

// Personality 3: single RGB color broadcast to all pixels + accent + strobe
void handleDMXPersonality3() {
  strobeRate1 = dmxBuffer[strobeChannel1];
  applyStrobe(strobeRate1);
  strobeRate2 = dmxBuffer[strobeChannel2];
  applyStrobe2(strobeRate2);

  if (led1State) {
    uint8_t r = dmxBuffer[RGBWstartAddress];
    uint8_t g = dmxBuffer[RGBWstartAddress + 1];
    uint8_t b = dmxBuffer[RGBWstartAddress + 2];
    CRGB col(apply_dimming(r, dimcurve), apply_dimming(g, dimcurve), apply_dimming(b, dimcurve));
    for (int i = 0; i < VEYRON_NUM_PIXELS_1; i++) leds1[i] = col;
  } else {
    for (int i = 0; i < VEYRON_NUM_PIXELS_1; i++) leds1[i] = CRGB(0, 0, 0);
  }

  if (led2State) {
    uint8_t d = apply_dimming(dmxBuffer[WhStartAddress], dimcurve);
    for (int i = 0; i < VEYRON_NUM_PIXELS_2; i++) leds2[i] = CRGB(d, d, d);
  } else {
    for (int i = 0; i < VEYRON_NUM_PIXELS_2; i++) leds2[i] = CRGB(0, 0, 0);
  }
  FastLED.show();
}

// Personality 4: mirror 20px RGB (60ch) + 2px accent + strobe
void handleDMXPersonality4() {
  strobeRate1 = dmxBuffer[strobeChannel1];
  applyStrobe(strobeRate1);
  strobeRate2 = dmxBuffer[strobeChannel2];
  applyStrobe2(strobeRate2);

  if (led1State) {
    for (int i = 0; i < VEYRON_NUM_PIXELS_1 / 2; i++) {
      int mirrored = 39 - i;
      uint8_t r = dmxBuffer[RGBWstartAddress + i * 3];
      uint8_t g = dmxBuffer[RGBWstartAddress + i * 3 + 1];
      uint8_t b = dmxBuffer[RGBWstartAddress + i * 3 + 2];
      CRGB col(apply_dimming(r, dimcurve), apply_dimming(g, dimcurve), apply_dimming(b, dimcurve));
      leds1[i] = col;
      leds1[mirrored] = col;
    }
  } else {
    for (int i = 0; i < VEYRON_NUM_PIXELS_1; i++) leds1[i] = CRGB(0, 0, 0);
  }

  if (led2State) {
    for (int i = 0; i < VEYRON_NUM_PIXELS_2; i++) {
      uint8_t d1 = apply_dimming(dmxBuffer[WhStartAddress + i * 3],     dimcurve);
      uint8_t d2 = apply_dimming(dmxBuffer[WhStartAddress + i * 3 + 1], dimcurve);
      uint8_t d3 = apply_dimming(dmxBuffer[WhStartAddress + i * 3 + 2], dimcurve);
      leds2[i] = (i == 0) ? CRGB(d2, d1, d3) : CRGB(d2, d3, d1);
    }
  } else {
    for (int i = 0; i < VEYRON_NUM_PIXELS_2; i++) leds2[i] = CRGB(0, 0, 0);
  }
  FastLED.show();
}

// Personality 5: grouped 2px per DMX channel (20ch -> 40px) + 2px accent + strobe
void handleDMXPersonality5() {
  strobeRate1 = dmxBuffer[strobeChannel1];
  applyStrobe(strobeRate1);
  strobeRate2 = dmxBuffer[strobeChannel2];
  applyStrobe2(strobeRate2);

  if (led1State) {
    const int groupSize = 2;
    int dmxGroups = VEYRON_NUM_PIXELS_1 / groupSize;
    for (int i = 0; i < dmxGroups; i++) {
      uint8_t r = dmxBuffer[RGBWstartAddress + i * 3];
      uint8_t g = dmxBuffer[RGBWstartAddress + i * 3 + 1];
      uint8_t b = dmxBuffer[RGBWstartAddress + i * 3 + 2];
      CRGB col(apply_dimming(r, dimcurve), apply_dimming(g, dimcurve), apply_dimming(b, dimcurve));
      leds1[i * groupSize]     = col;
      leds1[i * groupSize + 1] = col;
    }
  } else {
    for (int i = 0; i < VEYRON_NUM_PIXELS_1; i++) leds1[i] = CRGB(0, 0, 0);
  }

  if (led2State) {
    for (int i = 0; i < VEYRON_NUM_PIXELS_2; i++) {
      uint8_t d1 = apply_dimming(dmxBuffer[WhStartAddress + i * 3],     dimcurve);
      uint8_t d2 = apply_dimming(dmxBuffer[WhStartAddress + i * 3 + 1], dimcurve);
      uint8_t d3 = apply_dimming(dmxBuffer[WhStartAddress + i * 3 + 2], dimcurve);
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
    float strobecurve = 24.454 * pow(strobeRate, 0.423);
    float interval = map(strobecurve, 1, 255, strobeRateMin, strobeRateMax);
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
    float strobecurve = 24.454 * pow(strobeRate, 0.423);
    float interval = map(strobecurve, 1, 255, strobeRateMin, strobeRateMax);
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
