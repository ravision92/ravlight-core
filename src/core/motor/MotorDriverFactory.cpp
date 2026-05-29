#include "core/motor/MotorDriverFactory.h"

#ifdef RAVLIGHT_HAS_MOTOR

#include <string.h>
#include <esp_log.h>

static const char* TAG = "MotorFactory";

// Backend headers — compiled only when RAVLIGHT_HAS_MOTOR is defined.
#include "core/motor/backends/Tmc2209LocalDriver.h"
// #include "core/motor/backends/Tmc2209RemoteDriver.h"  — Fase 10
// #include "core/motor/backends/StepDirDriver.h"         — Fase 5

IMotorDriver* MotorDriverFactory::create(const JsonObject& cfg) {
    const char* backend = cfg["driver_backend"] | "";

    if (strcmp(backend, "tmc2209_local") == 0) {
        JsonObjectConst pins = cfg["pins"];
        if (pins.isNull()) {
            ESP_LOGE(TAG, "tmc2209_local: missing 'pins' object in config");
            return nullptr;
        }
        // Serial port: factory always uses Serial2; caller configures the board pins.
        return new Tmc2209LocalDriver(
            Serial2,
            cfg["tmc_address"] | 0,
            pins["step"] | 0,
            pins["dir"]  | 0,
            pins["en"]   | 0,
            pins["rx"]   | 0,
            pins["tx"]   | 0,
            pins["diag"] | 0
        );
    }

    if (strcmp(backend, "tmc2209_remote") == 0) {
        ESP_LOGW(TAG, "tmc2209_remote backend not yet implemented (Fase 10)");
        return nullptr;
    }

    if (strcmp(backend, "stepdir") == 0) {
        ESP_LOGW(TAG, "stepdir backend not yet implemented (Fase 5)");
        return nullptr;
    }

    ESP_LOGE(TAG, "unknown driver_backend: '%s'", backend);
    return nullptr;
}

#endif // RAVLIGHT_HAS_MOTOR
