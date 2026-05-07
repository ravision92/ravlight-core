#pragma once
#include <ArduinoJson.h>

// Unified fixture interface — each fixture implements all of these in its own
// fixtures/<name>/ files.  No #ifdef RAVLIGHT_FIXTURE_* needed in core code.

// Config (fixtures/<name>/fixture_config.cpp)
void fixtureConfigDefaults();
void fixtureConfigSerialize(JsonObject& fix);
void fixtureConfigDeserialize(const JsonObject& fix);
void fixtureGetDmxMap(JsonObject& map);

// DMX (fixtures/<name>/dmx_fixture.cpp)
void initFixture();
void handleDMX();
void startDMX();
void stopDMX();
void fixtureHighlight();   // visual identify pulse; no-op on fixtures that don't support it
