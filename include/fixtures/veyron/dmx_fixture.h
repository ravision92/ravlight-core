#ifndef DMX_FIXTURE_H
#define DMX_FIXTURE_H

#ifdef RAVLIGHT_FIXTURE_VEYRON

#include <Arduino.h>
#include "config.h"
#include <FastLED.h>
#include "FastLED_RGBW.h"

void initFixture();
void setPersonality(FixturePersonality personality);
void setDimCurve(uint16_t curve);
void setFixtureAddresses(int rgbwStart, int whStart, int strobeStart);
void handleDMX();
void handleDMXPersonality1();
void handleDMXPersonality2();
void handleDMXPersonality3();
void handleDMXPersonality4();
void handleDMXPersonality5();
void handlePixels1();
void handlePixels2();
void applyStrobe(uint8_t strobeRate);
void applyStrobe2(uint8_t strobeRate);
void applyStrobe3(uint8_t strobeRate);
void startHighlight();
void higliteSequence();
void startDMX();
void stopDMX();

#endif // RAVLIGHT_FIXTURE_VEYRON
#endif // DMX_FIXTURE_H
