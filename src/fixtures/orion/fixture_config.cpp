#ifdef RAVLIGHT_FIXTURE_ORION
#include "fixture_config.h"
#include "fixtures/orion/fixture.h"
#include "core/motor/IMotorDriver.h"
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

    fix["homingDirection"]    = orionConfig.homingDirection;
    fix["dmxInvertPosition"]  = orionConfig.dmxInvertPosition;

    fix["runCurrentMa"]       = orionConfig.runCurrentMa;
    fix["holdCurrentMa"]      = orionConfig.holdCurrentMa;
    fix["autoRehomeOnStall"]  = orionConfig.autoRehomeOnStall;
    fix["dmxWatchdogAction"]  = orionConfig.dmxWatchdogAction;
    fix["operSgthrs"]        = orionConfig.operSgthrs;
    fix["setupComplete"]     = orionConfig.setupComplete;
    fix["manualMode"]        = orionConfig.manualMode;
    fix["homeAtBoot"]        = orionConfig.homeAtBoot;
    fix["homeAtBootDelayMs"] = orionConfig.homeAtBootDelayMs;
    fix["keepHomeOnStall"]   = orionConfig.keepHomeOnStall;
    fix["dropAndRehome"]     = orionConfig.dropAndRehome;
    fix["dropWaitMs"]        = orionConfig.dropWaitMs;
    fix["sgConfidenceSigma"] = orionConfig.sgConfidenceSigma;

    // Adaptive SG profile — serialize bins as flat arrays for compact NVS round-trip.
    if (orionConfig.sgProfile.valid) {
        JsonObject sgp = fix.createNestedObject("sgProfile");
        sgp["base"] = orionConfig.sgProfile.base_speed;
        sgp["step"] = orionConfig.sgProfile.speed_step;
        JsonArray up = sgp.createNestedArray("up");
        JsonArray dn = sgp.createNestedArray("down");
        for (int i = 0; i < ORION_SGP_BINS; i++) {
            JsonArray u = up.createNestedArray();
            u.add(orionConfig.sgProfile.up[i].mean);
            u.add(orionConfig.sgProfile.up[i].stddev);
            JsonArray d = dn.createNestedArray();
            d.add(orionConfig.sgProfile.down[i].mean);
            d.add(orionConfig.sgProfile.down[i].stddev);
        }
    }

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
        if (o.gamma_x10 != 10) out["gamma"] = o.gamma_x10;   // forward-compat field; render uses it once wired in
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

// Tracks whether the last deserialize touched LED runtime-critical fields
// (count or protocol). Read + cleared by fixtureApplyLive() so callers know
// if an RMT re-init / restart is required.
static bool _orion_led_needs_restart = false;

void fixtureConfigDeserialize(const JsonObject& fix) {
    // Critical: the LED-restart flag is a static bool. The boot-time
    // loadConfig() also calls this deserialize, and during boot the
    // snapshot below reads the just-applied compile-time defaults while
    // the JSON from NVS may differ — that "change" used to leave the
    // flag stuck true, so the first user save reported a spurious
    // restart. Wipe it on entry so each call is fresh.
    _orion_led_needs_restart = false;
#ifdef ORION_HAS_LED
    led_protocol_t _prev_proto[HW_LED_OUTPUT_COUNT];
    uint16_t       _prev_count[HW_LED_OUTPUT_COUNT];
    for (int i = 0; i < HW_LED_OUTPUT_COUNT; i++) {
        _prev_proto[i] = orionConfig.ledOutputs[i].protocol;
        _prev_count[i] = orionConfig.ledOutputs[i].pixel_count;
    }
#endif
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
    // Motion envelope hard caps. UI mirrors these via /api/motor-limits;
    // clamp here is the last-resort safety net if anyone POSTs directly
    // (curl, unmigrated saved configs, DMX config-write extensions).
    if (orionConfig.maxSpeed < 100)                    orionConfig.maxSpeed = 100;
    if (orionConfig.maxSpeed > ORION_MAX_SPEED_STEPS)  orionConfig.maxSpeed = ORION_MAX_SPEED_STEPS;
    if (orionConfig.maxAccel < 100)                    orionConfig.maxAccel = 100;
    if (orionConfig.maxAccel > ORION_MAX_ACCEL_STEPS)  orionConfig.maxAccel = ORION_MAX_ACCEL_STEPS;
    if (orionConfig.jogSpeed < 100)                    orionConfig.jogSpeed = 100;
    if (orionConfig.jogSpeed > ORION_MAX_JOG_STEPS)    orionConfig.jogSpeed = ORION_MAX_JOG_STEPS;

    orionConfig.drumDiameterMm   = fix["drumDiameterMm"]   | orionConfig.drumDiameterMm;
    orionConfig.motorStepsPerRev = fix["motorStepsPerRev"] | orionConfig.motorStepsPerRev;
    orionConfig.gearRatio        = fix["gearRatio"]        | orionConfig.gearRatio;

    orionConfig.homingDirection    = fix["homingDirection"]    | orionConfig.homingDirection;
    orionConfig.dmxInvertPosition  = fix["dmxInvertPosition"]  | orionConfig.dmxInvertPosition;

    orionConfig.runCurrentMa      = fix["runCurrentMa"]      | orionConfig.runCurrentMa;
    orionConfig.holdCurrentMa     = fix["holdCurrentMa"]     | orionConfig.holdCurrentMa;
    // Safety clamp: never exceed 3000 mA (max safe for typical NEMA17/23 at 24 V)
    if (orionConfig.runCurrentMa  > 3000) orionConfig.runCurrentMa  = ORION_RUN_CURRENT_MA;
    if (orionConfig.holdCurrentMa > orionConfig.runCurrentMa)
        orionConfig.holdCurrentMa = orionConfig.runCurrentMa / 10;
    orionConfig.autoRehomeOnStall = fix["autoRehomeOnStall"] | orionConfig.autoRehomeOnStall;
    orionConfig.dmxWatchdogAction = fix["dmxWatchdogAction"] | orionConfig.dmxWatchdogAction;
    // Clamp to the enum range [0..2]. Anything else (from legacy configs or
    // out-of-range POSTs) reverts to the safe default ESTOP.
    if (orionConfig.dmxWatchdogAction > (uint8_t)OrionWatchdogAction::DO_NOTHING)
        orionConfig.dmxWatchdogAction = (uint8_t)OrionWatchdogAction::ESTOP;
    orionConfig.operSgthrs        = fix["operSgthrs"]        | orionConfig.operSgthrs;
    orionConfig.setupComplete     = fix["setupComplete"]     | orionConfig.setupComplete;
    orionConfig.manualMode        = fix["manualMode"]        | orionConfig.manualMode;
    orionConfig.homeAtBoot        = fix["homeAtBoot"]        | orionConfig.homeAtBoot;
    orionConfig.homeAtBootDelayMs = fix["homeAtBootDelayMs"] | orionConfig.homeAtBootDelayMs;
    orionConfig.keepHomeOnStall   = fix["keepHomeOnStall"]   | orionConfig.keepHomeOnStall;
    orionConfig.dropAndRehome     = fix["dropAndRehome"]     | orionConfig.dropAndRehome;
    orionConfig.dropWaitMs        = fix["dropWaitMs"]        | orionConfig.dropWaitMs;
    orionConfig.sgConfidenceSigma = fix["sgConfidenceSigma"] | orionConfig.sgConfidenceSigma;
    // Clamp the ranges — same defense-in-depth pattern as motion envelope.
    if (orionConfig.homeAtBootDelayMs < 100)   orionConfig.homeAtBootDelayMs = 100;
    if (orionConfig.homeAtBootDelayMs > 5000)  orionConfig.homeAtBootDelayMs = 5000;
    if (orionConfig.dropWaitMs < 100)          orionConfig.dropWaitMs = 100;
    if (orionConfig.dropWaitMs > 10000)        orionConfig.dropWaitMs = 10000;
    if (orionConfig.sgConfidenceSigma < 2)     orionConfig.sgConfidenceSigma = 2;
    if (orionConfig.sgConfidenceSigma > 5)     orionConfig.sgConfidenceSigma = 5;
    // Defensive clamp: a saved operSgthrs below ~10 is the sign of a calibration
    // that ran with a too-slow motor (StallGuard4 noise). Such values cause
    // immediate fake stalls during normal motion. Floor to a safer minimum so
    // the user can still operate while they redo SGCal with a valid speed.
    if (orionConfig.operSgthrs < 10) orionConfig.operSgthrs = ORION_SGTHRS;

    // Adaptive SG profile — populate from JSON, mark valid only when all bins
    // and both directions parsed successfully.
    JsonObjectConst sgp = fix["sgProfile"].as<JsonObjectConst>();
    if (!sgp.isNull()) {
        orionConfig.sgProfile.base_speed = sgp["base"] | (uint32_t)0;
        orionConfig.sgProfile.speed_step = sgp["step"] | (uint32_t)0;
        JsonArrayConst up = sgp["up"].as<JsonArrayConst>();
        JsonArrayConst dn = sgp["down"].as<JsonArrayConst>();
        bool ok = (up.size() == ORION_SGP_BINS) && (dn.size() == ORION_SGP_BINS);
        for (int i = 0; i < ORION_SGP_BINS && ok; i++) {
            orionConfig.sgProfile.up[i].mean     = up[i][0]  | (uint16_t)0;
            orionConfig.sgProfile.up[i].stddev   = up[i][1]  | (uint16_t)0;
            orionConfig.sgProfile.down[i].mean   = dn[i][0]  | (uint16_t)0;
            orionConfig.sgProfile.down[i].stddev = dn[i][1]  | (uint16_t)0;
        }
        orionConfig.sgProfile.valid = ok && (orionConfig.sgProfile.base_speed > 0);
    }

#ifdef ORION_HAS_LED
    JsonArrayConst outputs = fix["ledOutputs"].as<JsonArrayConst>();
    if (outputs.isNull() || outputs.size() < HW_LED_OUTPUT_COUNT) {
        // The JSON payload didn't contain a full ledOutputs array. Skip the
        // LED loop entirely so we don't silently reset every output to
        // defaults (which would also trigger a spurious restart).
        return;
    }
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
        o.gamma_x10      = out["gamma"] | (uint8_t)10;
        if (o.gamma_x10 < 10) o.gamma_x10 = 10;
        if (o.gamma_x10 > 30) o.gamma_x10 = 30;
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
        if (o.protocol != _prev_proto[i] || o.pixel_count != _prev_count[i]) {
            _orion_led_needs_restart = true;
        }
    }
#endif
}

// Apply live-updatable changes to the motor driver and report whether a
// restart is still needed for the rest. Motor params (homing direction,
// soft limits, currents, jog speed, mechanical calibration) all take effect
// immediately via orionApplyHomingConfig() + orionApplySoftLimitsExternal().
// LED count/protocol changes still require an RMT re-init → restart.
bool fixtureApplyLive() {
    // Skip homing-config push while homing is active — _hcfg is a multi-field
    // struct; writing it from core 0 while _updateHoming() reads it on core 1
    // is a race that can corrupt the backoff direction and cause unexpected motion.
    IMotorDriver* drv = orionGetDriver();
    if (!drv || drv->getStatus().state != MotorState::HOMING) {
        orionApplyHomingConfig();
    }
    orionApplySoftLimitsExternal();
    orionApplyMotorCurrents();   // deferred to motor task via flag — safe from any core
    // Propagate maxSpeed / maxAccel to the driver's ramp generator. Without
    // this, edits to Motor step in the wizard only take effect after a
    // reboot or on the next STANDARD-personality DMX frame — a hidden bug
    // that made live tuning inconsistent and produced deceleration mismatch
    // (stall-on-braking) when the operator lowered maxAccel expecting
    // gentler stops.
    if (drv) {
        drv->setSpeed((float)orionConfig.maxSpeed);
        drv->setAccel((float)orionConfig.maxAccel);
    }
    bool need = _orion_led_needs_restart;
    _orion_led_needs_restart = false;
    return need;
}

// ── DMX map for /dmxmonitor ─────────────────────────────────────────────────

// Orion has no effect-side function channels (the motor channels are
// DMX inputs, not outputs of the effects engine). No-op overlay.
void fixtureApplyEffectFunctions(uint8_t* buf, uint16_t universe) {
    (void)buf; (void)universe;
}

uint8_t fixtureGetEffectTargets(fx_target_t* out, uint8_t max) {
#ifdef ORION_HAS_LED
    uint8_t n = 0;
    for (int i = 0; i < HW_LED_OUTPUT_COUNT && n < max; i++) {
        const led_output_cfg_t& o = orionConfig.ledOutputs[i];
        if (o.pixel_count == 0)             continue;
        if (o.protocol == LED_PWM)          continue;
        if (o.protocol == LED_RELAY)        continue;
        if (o.protocol == LED_CLOCK_FOLLOWER) continue;
        out[n].universe     = o.universe_start;
        out[n].dmx_start    = o.dmx_start;
        out[n].pixel_count  = o.pixel_count;
        out[n].ch_per_pixel = led_ch_per_pixel(o.protocol);
        n++;
    }
    return n;
#else
    (void)out; (void)max;
    // Motor-only Orion has no addressable pixels — effects engine
    // produces no output on this fixture.
    return 0;
#endif
}

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
