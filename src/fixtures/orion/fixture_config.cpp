#ifdef RAVLIGHT_FIXTURE_ORION
#include "fixture_config.h"
#include "fixtures/orion/fixture.h"
#include "config.h"
#include "dmx_manager.h"
#include <ArduinoJson.h>
#include <esp_log.h>

static const char* TAG = "ORION_CFG";

OrionConfig orionConfig;

// Steps per cm of real fixture travel, from the mechanical calibration.
// One drum revolution = motorStepsPerRev x 16 microsteps x gearRatio steps, and
// moves the fixture by the drum circumference (pi x diameter).
float orionStepsPerCm() {
    float circumferenceCm = 3.14159265f * (orionConfig.drumDiameterMm / 10.0f);
    float stepsPerRev     = (float)orionConfig.motorStepsPerRev * 16.0f * orionConfig.gearRatio;
    if (circumferenceCm < 0.1f || stepsPerRev < 1.0f) return 1.0f;  // bad calibration -> safe fallback
    return stepsPerRev / circumferenceCm;
}

// ── Config persistence ──────────────────────────────────────────────────────

void fixtureConfigDefaults() {
    orionConfig = OrionConfig{};   // zero-init via member defaults
#ifdef ORION_HAS_LED
    for (int i = 0; i < HW_LED_OUTPUT_COUNT; i++) {
        led_output_cfg_t& o = orionConfig.ledOutputs[i];
        o.protocol       = LED_WS2812B;
        o.pixel_count    = 0;            // disabled until configured from the UI
        o.universe_start = (uint16_t)i;
        o.dmx_start      = 1;
        o.grouping       = 1;
        o.invert         = 0;
        o.brightness     = 255;
        o.color_order[0] = 0; o.color_order[1] = 1;
        o.color_order[2] = 2; o.color_order[3] = 3;
        o.pwm_freq_hz    = 0;
        o.pwm_curve      = 0;
        o.pwm_16bit      = 0;
        o.pwm_invert     = 0;
        o.relay_threshold = 128;
    }
#endif
    ESP_LOGI(TAG, "Orion config: defaults loaded");
}

void fixtureConfigSerialize(JsonObject& fix) {
    fix["personality"]     = orionConfig.personality;
    fix["positionStart"]   = orionConfig.positionStart;
    fix["controlStart"]    = orionConfig.controlStart;

    fix["downPosition"]    = orionConfig.downPosition;
    fix["upPosition"]      = orionConfig.upPosition;
    fix["maxSpeed"]        = orionConfig.maxSpeed;
    fix["maxAccel"]        = orionConfig.maxAccel;
    fix["jogSpeed"]        = orionConfig.jogSpeed;

    fix["drumDiameterMm"]   = orionConfig.drumDiameterMm;
    fix["motorStepsPerRev"] = orionConfig.motorStepsPerRev;
    fix["gearRatio"]        = orionConfig.gearRatio;

    fix["homingDirection"] = orionConfig.homingDirection;

    fix["dmxWatchdogAction"] = orionConfig.dmxWatchdogAction;
    fix["operSgthrs"]        = orionConfig.operSgthrs;

#ifdef ORION_HAS_LED
    JsonArray outputs = fix.createNestedArray("ledOutputs");
    for (int i = 0; i < HW_LED_OUTPUT_COUNT; i++) {
        const led_output_cfg_t& o = orionConfig.ledOutputs[i];
        JsonObject out = outputs.createNestedObject();
        out["proto"] = (int)o.protocol;
        out["count"] = o.pixel_count;
        out["univ"]  = o.universe_start;
        out["ch"]    = o.dmx_start;
        out["group"] = o.grouping;
        out["inv"]   = o.invert;
        out["bri"]   = o.brightness;
        if (o.protocol == LED_RELAY) {
            out["relay_thr"] = o.relay_threshold;
        } else if (o.protocol == LED_PWM) {
            out["pwm_freq"]  = o.pwm_freq_hz;
            out["pwm_curve"] = o.pwm_curve;
            out["pwm_16bit"] = o.pwm_16bit;
            out["pwm_inv"]   = o.pwm_invert;
        } else {
            char order_str[5];
            color_order_to_str(o.color_order, led_ch_per_pixel(o.protocol), order_str);
            out["order"] = order_str;
        }
    }
#endif
}

void fixtureConfigDeserialize(const JsonObject& fix) {
    orionConfig.personality   = fix["personality"]   | orionConfig.personality;
    orionConfig.positionStart = fix["positionStart"] | orionConfig.positionStart;
    orionConfig.controlStart  = fix["controlStart"]  | orionConfig.controlStart;

    // Accept legacy names softMinPosition/softMaxPosition for backward compat
    orionConfig.downPosition    = fix["downPosition"]
                                | fix["softMinPosition"]
                                | orionConfig.downPosition;
    orionConfig.upPosition      = fix["upPosition"]
                                | fix["softMaxPosition"]
                                | orionConfig.upPosition;
    orionConfig.maxSpeed        = fix["maxSpeed"]        | orionConfig.maxSpeed;
    orionConfig.maxAccel        = fix["maxAccel"]        | orionConfig.maxAccel;
    orionConfig.jogSpeed        = fix["jogSpeed"]        | orionConfig.jogSpeed;

    orionConfig.drumDiameterMm   = fix["drumDiameterMm"]   | orionConfig.drumDiameterMm;
    orionConfig.motorStepsPerRev = fix["motorStepsPerRev"] | orionConfig.motorStepsPerRev;
    orionConfig.gearRatio        = fix["gearRatio"]        | orionConfig.gearRatio;

    orionConfig.homingDirection = fix["homingDirection"] | orionConfig.homingDirection;

    orionConfig.dmxWatchdogAction = fix["dmxWatchdogAction"] | orionConfig.dmxWatchdogAction;
    orionConfig.operSgthrs        = fix["operSgthrs"]        | orionConfig.operSgthrs;

#ifdef ORION_HAS_LED
    JsonArrayConst outputs = fix["ledOutputs"].as<JsonArrayConst>();
    for (int i = 0; i < HW_LED_OUTPUT_COUNT; i++) {
        led_output_cfg_t& o  = orionConfig.ledOutputs[i];
        JsonObjectConst out  = outputs[i].as<JsonObjectConst>();
        o.protocol       = (led_protocol_t)(out["proto"] | (int)LED_WS2812B);
        o.pixel_count    = out["count"] | (uint16_t)0;
        o.universe_start = out["univ"]  | (uint16_t)i;
        o.dmx_start      = out["ch"]    | (uint16_t)1;
        o.grouping       = out["group"] | (uint8_t)1;
        o.invert         = out["inv"]   | (uint8_t)0;
        o.brightness     = out["bri"]   | (uint8_t)255;
        if (o.grouping == 0) o.grouping = 1;
        o.pwm_freq_hz     = out["pwm_freq"]   | (uint16_t)0;
        o.pwm_curve       = out["pwm_curve"]  | (uint8_t)0;
        o.pwm_16bit       = out["pwm_16bit"]  | (uint8_t)0;
        o.pwm_invert      = out["pwm_inv"]    | (uint8_t)0;
        o.relay_threshold = out["relay_thr"]  | (uint8_t)128;
        const char* order_str = out["order"] | "";
        if (order_str[0]) {
            color_order_from_str(order_str, o.color_order);
        } else {
            o.color_order[0] = 0; o.color_order[1] = 1;
            o.color_order[2] = 2; o.color_order[3] = 3;
        }
    }
#endif
}

// ── DMX map for /dmxmonitor ─────────────────────────────────────────────────

// DMX Monitor schema: { "<universe>": [[lo,hi], ...] } — same shape Veyron/Elyon
// emit, so the monitor's universe selector and channel highlighting work.
void fixtureGetDmxMap(JsonObject& map) {
    OrionPersonality p = (OrionPersonality)orionConfig.personality;

    auto addRange = [&](uint16_t universe, uint16_t lo, uint16_t hi) {
        char uKey[8];
        snprintf(uKey, sizeof(uKey), "%u", universe);
        JsonArray arr = map.containsKey(uKey) ? map[uKey].as<JsonArray>()
                                              : map.createNestedArray(uKey);
        JsonArray r = arr.createNestedArray();
        r.add(lo);
        r.add(hi);
    };

    // Motor blocks live in the global start universe.
    uint16_t mu = dmxConfig.startUniverse;
    uint16_t pb = orionConfig.positionStart;
    uint8_t  pb_size = (p == OrionPersonality::BASIC) ? 1 : 2;
    addRange(mu, pb, pb + pb_size - 1);

    uint16_t cb = orionConfig.controlStart;
    uint8_t  cb_size = (p == OrionPersonality::STANDARD) ? 4 : 3;
    addRange(mu, cb, cb + cb_size - 1);

#ifdef ORION_HAS_LED
    // LED outputs occupy their own universes (may span several).
    for (int i = 0; i < HW_LED_OUTPUT_COUNT; i++) {
        const led_output_cfg_t& out = orionConfig.ledOutputs[i];
        if (led_dmx_slots(&out) == 0) continue;
        uint16_t remaining = (uint16_t)led_dmx_channels(&out);
        uint16_t u   = out.universe_start;
        uint16_t pos = out.dmx_start;
        while (remaining > 0) {
            uint16_t end = (pos + remaining - 1 > 512) ? 512 : (pos + remaining - 1);
            addRange(u, pos, end);
            remaining -= (end - pos + 1);
            u++;
            pos = 1;
        }
    }
#endif
}

#endif // RAVLIGHT_FIXTURE_ORION
