#pragma once
#include <ArduinoJson.h>

// Fixture config interface — implemented by each fixture in fixtures/<name>/fixture_config.cpp.
// config.cpp calls these without any #ifdef RAVLIGHT_FIXTURE_* knowledge.

void fixtureConfigDefaults();
void fixtureConfigSerialize(JsonObject& fix);
void fixtureConfigDeserialize(const JsonObject& fix);

// Returns the DMX channel map for the active fixture.
// map: JsonObject keyed by universe number (as string); each value is an
// array of [start, end] channel ranges (1-indexed). Used by /dmxmap endpoint.
void fixtureGetDmxMap(JsonObject& map);
