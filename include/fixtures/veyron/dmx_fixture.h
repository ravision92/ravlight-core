#ifndef DMX_FIXTURE_H
#define DMX_FIXTURE_H

#ifdef RAVLIGHT_FIXTURE_VEYRON

#include "config.h"

void initFixture();
void setPersonality(FixturePersonality personality);
void setDimCurve(uint16_t curve);
void setFixtureAddresses(int rgbwStart, int whStart, int functionStart);
void handleDMX();
void handleDMXPersonality1();
void handleDMXPersonality2();
void handleDMXPersonality3();
void handleDMXPersonality4();
void handleDMXPersonality5();
void handleDMXPersonality6();
void handleDMXPersonality7();
void handleDMXPersonality8();
void handleDMXPersonality9();
void applyStrobe(uint8_t strobeRate);
void applyStrobe2(uint8_t strobeRate);
void startHighlight();
void higliteSequence();
void startDMX();
void stopDMX();

// Pushes a freshly fixtureConfigDeserialize()'d veyronConfig into the
// runtime patch state (veyron_patch, dimcurve, RDM start address) used by
// the renderer — see fixture_config.cpp's fixtureApplyLive().
void applyVeyronConfigLive();

// Pumps the status LED overlay outside handleDMX() — see fixture_config.h.
void fixtureTickStatus();

#endif // RAVLIGHT_FIXTURE_VEYRON
#endif // DMX_FIXTURE_H
