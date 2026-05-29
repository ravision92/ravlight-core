#pragma once
#include <ArduinoJson.h>
#include "IMotorDriver.h"

// Creates the appropriate IMotorDriver backend from a config JSON object.
//
// Expected JSON shape (section "motor" inside fixture config):
//   { "driver_backend": "tmc2209_local" | "tmc2209_remote" | "stepdir", ... }
//
// Returns nullptr on missing or unrecognised driver_backend.
// Caller owns the returned pointer and must delete it via end() + delete.

class MotorDriverFactory {
public:
    static IMotorDriver* create(const JsonObject& cfg);
};
