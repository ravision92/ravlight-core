#pragma once
#include <ArduinoJson.h>

// Fixture config interface — implemented by each fixture in fixtures/<name>/fixture_config.cpp.
// config.cpp calls these without any #ifdef RAVLIGHT_FIXTURE_* knowledge.

void fixtureConfigDefaults();
void fixtureConfigSerialize(JsonObject& fix);
void fixtureConfigDeserialize(const JsonObject& fix);
