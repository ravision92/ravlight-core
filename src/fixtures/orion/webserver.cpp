#ifdef RAVLIGHT_FIXTURE_ORION
#include "fixture_webserver.h"
#include "fixtures/orion/fixture.h"
#include "core/motor/IMotorDriver.h"
#include "config.h"
#include "dmx_manager.h"
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <esp_log.h>

static const char* TAG = "ORION_WEB";

void handleFixtureSaveParams(AsyncWebServerRequest* request, bool& needsRestart) {
    // Motor params apply live; LED pixel-count / protocol changes need a restart
    // (RMT channels are sized at init time).
    bool changed = false;

    auto setU8 = [&](const char* name, uint8_t& field) {
        if (request->hasParam(name, true)) {
            uint8_t v = (uint8_t)request->getParam(name, true)->value().toInt();
            if (v != field) { field = v; changed = true; }
        }
    };
    auto setU16 = [&](const char* name, uint16_t& field) {
        if (request->hasParam(name, true)) {
            uint16_t v = (uint16_t)request->getParam(name, true)->value().toInt();
            if (v != field) { field = v; changed = true; }
        }
    };

    // cm-based fields → converted to steps internally. Float value in form, integer step in config.
    auto setCmToStepsI = [&](const char* name, int32_t& field) {
        if (request->hasParam(name, true)) {
            float cm = request->getParam(name, true)->value().toFloat();
            int32_t v = (int32_t)(cm * orionStepsPerCm());
            if (v != field) { field = v; changed = true; }
        }
    };
    auto setCmToStepsU = [&](const char* name, uint32_t& field) {
        if (request->hasParam(name, true)) {
            float cm = request->getParam(name, true)->value().toFloat();
            if (cm < 0) cm = 0;
            uint32_t v = (uint32_t)(cm * orionStepsPerCm());
            if (v != field) { field = v; changed = true; }
        }
    };

    setU8 ("personality",     orionConfig.personality);
    setU16("positionStart",   orionConfig.positionStart);
    setU16("controlStart",    orionConfig.controlStart);
    setCmToStepsI("downCm", orionConfig.downPosition);
    setCmToStepsI("upCm",   orionConfig.upPosition);
    setCmToStepsU("maxSpeedCm", orionConfig.maxSpeed);
    setCmToStepsU("maxAccelCm", orionConfig.maxAccel);
    setCmToStepsU("jogSpeedCm", orionConfig.jogSpeed);
    setU8 ("dmxWatchdogAction", orionConfig.dmxWatchdogAction);
    setU16("drumDiaMm",     orionConfig.drumDiameterMm);
    setU16("motorStepsRev", orionConfig.motorStepsPerRev);
    if (request->hasParam("gearRatio", true)) {
        float g = request->getParam("gearRatio", true)->value().toFloat();
        if (g >= 0.01f && g != orionConfig.gearRatio) { orionConfig.gearRatio = g; changed = true; }
    }

    if (request->hasParam("homingDirection", true)) {
        int8_t v = (int8_t)request->getParam("homingDirection", true)->value().toInt();
        if (v != orionConfig.homingDirection) {
            orionConfig.homingDirection = (v >= 0) ? 1 : -1;
            changed = true;
            orionApplyHomingConfig();   // propagate to running driver
        }
    }

#ifdef ORION_HAS_LED
    for (int i = 0; i < HW_LED_OUTPUT_COUNT; i++) {
        led_output_cfg_t& o = orionConfig.ledOutputs[i];
        String s = String(i);
        auto ledInt = [&](const char* base, long def) -> long {
            String k = String(base) + s;
            return request->hasParam(k, true) ? request->getParam(k, true)->value().toInt() : def;
        };
        led_protocol_t newProto = (led_protocol_t)ledInt("orionLedProto", (int)o.protocol);
        uint16_t       newCount = (uint16_t)ledInt("orionLedCount", o.pixel_count);
        if (newProto != o.protocol || newCount != o.pixel_count) { needsRestart = true; changed = true; }
        o.protocol    = newProto;
        o.pixel_count = newCount;

        uint16_t newUniv = (uint16_t)ledInt("orionLedUniv",  o.universe_start);
        uint16_t newCh   = (uint16_t)ledInt("orionLedCh",    o.dmx_start);
        uint8_t  newBri  = (uint8_t) ledInt("orionLedBri",   o.brightness);
        uint8_t  newGrp  = (uint8_t) ledInt("orionLedGroup", o.grouping);
        if (newGrp == 0) newGrp = 1;
        uint8_t  newInv  = request->hasParam("orionLedInv" + s, true) ? 1 : 0;
        if (newUniv != o.universe_start || newCh != o.dmx_start || newBri != o.brightness ||
            newGrp != o.grouping || newInv != o.invert) changed = true;
        o.universe_start = newUniv;
        o.dmx_start      = newCh;
        o.brightness     = newBri;
        o.grouping       = newGrp;
        o.invert         = newInv;

        String ok = "orionLedOrder" + s;
        if (request->hasParam(ok, true)) {
            String v = request->getParam(ok, true)->value();
            v.toUpperCase();
            color_order_from_str(v.c_str(), o.color_order);
            changed = true;
        }
    }
#endif

    // Apply live: soft limits depend on the down/up positions just saved.
    if (changed) orionApplySoftLimitsExternal();
}

// ── Fixture-specific routes ─────────────────────────────────────────────────

static const char* stateName(MotorState s) {
    switch (s) {
        case MotorState::IDLE:       return "idle";
        case MotorState::HOMING:     return "homing";
        case MotorState::MOVING:     return "moving";
        case MotorState::JOGGING:    return "jogging";
        case MotorState::FAULT:      return "fault";
        case MotorState::DRIVER_OFF: return "driver_off";
    }
    return "unknown";
}

void registerFixtureRoutes(AsyncWebServer& server) {
    // GET /motorstatus — JSON snapshot for UI polling
    server.on("/motorstatus", HTTP_GET, [](AsyncWebServerRequest* req) {
        IMotorDriver* drv = orionGetDriver();
        StaticJsonDocument<512> doc;

        if (!drv) {
            doc["available"] = false;
            doc["error"]     = "driver not initialised";
        } else {
            MotorStatus s = drv->getStatus();
            doc["available"]    = true;
            doc["state"]        = stateName(s.state);
            doc["faultFlags"]   = s.fault_flags;
            doc["sgResult"]     = s.sg_result;
            doc["driverTemp"]   = s.driver_temp;
            doc["homed"]        = s.homed;
            doc["busy"]         = drv->isBusy();
            // cm-converted values for UI (cm-native)
            const float spc = orionStepsPerCm();
            doc["positionCm"] = (float)s.position / spc;
            doc["speedCm"]    = s.speed / spc;
            doc["downCm"]     = (float)orionConfig.downPosition / spc;
            doc["upCm"]       = (float)orionConfig.upPosition / spc;
            doc["dmxActive"]  = dmxIsActive();
            doc["dmxEnable"]  = orionDmxLastEnable();
            doc["override"]   = orionManualOverride();
            doc["operSgthrs"] = orionConfig.operSgthrs;
            doc["homingSpeed"]= ORION_HOMING_SPEED;
        }

        String out;
        serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    // POST /home — trigger sensorless homing
    server.on("/home", HTTP_POST, [](AsyncWebServerRequest* req) {
        IMotorDriver* drv = orionGetDriver();
        if (!drv) { req->send(503, "text/plain", "driver unavailable"); return; }
        drv->startHoming();
        ESP_LOGI(TAG, "homing triggered via /home");
        req->send(200, "text/plain", "homing started");
    });

    // POST /sethome — manual override: zero the position counter and declare
    // homed at the current physical position. Use when sensorless homing isn't
    // available (driver without current sensing) or when "home" is a chosen
    // operating position rather than a mechanical end stop.
    server.on("/sethome", HTTP_POST, [](AsyncWebServerRequest* req) {
        IMotorDriver* drv = orionGetDriver();
        if (!drv) { req->send(503, "text/plain", "driver unavailable"); return; }
        drv->setHomePosition();
        ESP_LOGI(TAG, "home set manually via /sethome");
        req->send(200, "text/plain", "home set");
    });

    // POST /sgcal?dir=up|down — start a StallGuard calibration run. Motor turns at
    // homing speed in the chosen direction (default = homing direction), stall fault
    // suppressed so the operator can load the shaft and read sg. Gated while DMX armed.
    server.on("/sgcal", HTTP_POST, [](AsyncWebServerRequest* req) {
        IMotorDriver* drv = orionGetDriver();
        if (!drv) { req->send(503, "text/plain", "driver unavailable"); return; }
        if (dmxIsActive() && orionDmxLastEnable() != 0) {
            req->send(409, "text/plain", "DMX is armed — set Enable to 0 on the console first");
            return;
        }
        int8_t d = orionConfig.homingDirection;
        if (req->hasParam("dir", true)) {
            String s = req->getParam("dir", true)->value();
            if (s == "up") d = +1; else if (s == "down") d = -1;
        }
        drv->startStallGuardCal(d);
        ESP_LOGI(TAG, "StallGuard calibration started via /sgcal (dir=%d)", (int)d);
        req->send(200, "text/plain", "sg cal running");
    });

    // POST /sgcalstop — stop the StallGuard calibration run
    server.on("/sgcalstop", HTTP_POST, [](AsyncWebServerRequest* req) {
        IMotorDriver* drv = orionGetDriver();
        if (!drv) { req->send(503, "text/plain", "driver unavailable"); return; }
        drv->stopStallGuardCal();
        ESP_LOGI(TAG, "StallGuard calibration stopped via /sgcalstop");
        req->send(200, "text/plain", "sg cal stopped");
    });

    // POST /sgthreshold?value=N — save the operating StallGuard threshold computed
    // by the calibration wizard (1..255). Persists to NVS and applies live.
    server.on("/sgthreshold", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (!req->hasParam("value", true)) { req->send(400, "text/plain", "missing value"); return; }
        int v = req->getParam("value", true)->value().toInt();
        if (v < 1)   v = 1;
        if (v > 255) v = 255;
        orionSetOperSgthrs((uint8_t)v);
        req->send(200, "text/plain", String(v));
    });

    // POST /clearfault — recover from FAULT
    server.on("/clearfault", HTTP_POST, [](AsyncWebServerRequest* req) {
        IMotorDriver* drv = orionGetDriver();
        if (!drv) { req->send(503, "text/plain", "driver unavailable"); return; }
        bool ok = drv->clearFault();
        ESP_LOGI(TAG, "clearfault via /clearfault: %s", ok ? "ok" : "still faulted");
        req->send(ok ? 200 : 409, "text/plain", ok ? "cleared" : "fault persists");
    });

    // POST /estop — emergency stop
    server.on("/estop", HTTP_POST, [](AsyncWebServerRequest* req) {
        IMotorDriver* drv = orionGetDriver();
        if (!drv) { req->send(503, "text/plain", "driver unavailable"); return; }
        drv->estop();
        ESP_LOGW(TAG, "e-stop via /estop");
        req->send(200, "text/plain", "estopped");
    });

    // POST /moveto?pos=N — manual position command for testing (bypasses DMX)
    server.on("/moveto", HTTP_POST, [](AsyncWebServerRequest* req) {
        IMotorDriver* drv = orionGetDriver();
        if (!drv) { req->send(503, "text/plain", "driver unavailable"); return; }
        if (!req->hasParam("pos", true)) {
            req->send(400, "text/plain", "missing pos");
            return;
        }
        int32_t pos = req->getParam("pos", true)->value().toInt();
        drv->moveTo(pos);
        req->send(200, "text/plain", "ok");
    });

    // POST /highlight — visual identify jog
    server.on("/highlight", HTTP_POST, [](AsyncWebServerRequest* req) {
        fixtureHighlight();
        req->send(200, "text/plain", "ok");
    });

    // POST /jog?dir=up|down — start continuous jog at jogSpeed (hold-to-jog UX).
    // "up" maps to +1 direction, "down" to -1. The caller (UI) should issue
    // /jogstop on button release. Bypasses soft limits — calibration only.
    //
    // Safety gate: refused while DMX is actively armed (Enable != 0). Operator
    // must disarm the console first to avoid fighting between DMX target and jog.
    server.on("/jog", HTTP_POST, [](AsyncWebServerRequest* req) {
        IMotorDriver* drv = orionGetDriver();
        if (!drv) { req->send(503, "text/plain", "driver unavailable"); return; }
        if (dmxIsActive() && orionDmxLastEnable() != 0) {
            req->send(409, "text/plain", "DMX is armed — set Enable to 0 on the console first");
            return;
        }
        if (!req->hasParam("dir", true)) {
            req->send(400, "text/plain", "missing dir"); return;
        }
        String dir = req->getParam("dir", true)->value();
        int8_t d = (dir == "up") ? +1 : (dir == "down") ? -1 : 0;
        if (d == 0) { req->send(400, "text/plain", "dir must be up or down"); return; }
        // Before homing, clamp jog to a safe slow speed: the position counter
        // is meaningless and the operator is typically searching for the end
        // stop or limit positions. After homing, use the user-configured speed.
        float jogSpeed = (float)orionConfig.jogSpeed;
        if (!drv->getStatus().homed && jogSpeed > 1500.0f) jogSpeed = 1500.0f;
        // Optional soft-limit override (set from the manual-jog "Override"
        // toggle in the UI). Lets the operator drive past saved limits to
        // define new ones. Cleared on /jogstop.
        bool override_limits = req->hasParam("override", true) &&
                               req->getParam("override", true)->value() == "1";
        orionSetJogIgnoreLimits(override_limits);
        drv->jog(d, jogSpeed);
        orionEnterManualOverride();   // suppress DMX motion until /release-dmx
        req->send(200, "text/plain", "jogging");
    });

    // POST /jogstop — end the current jog burst. Stays in manual override
    // until /release-dmx is called explicitly.
    server.on("/jogstop", HTTP_POST, [](AsyncWebServerRequest* req) {
        IMotorDriver* drv = orionGetDriver();
        if (!drv) { req->send(503, "text/plain", "driver unavailable"); return; }
        drv->stop();
        orionSetJogIgnoreLimits(false);   // clear any override from this session
        req->send(200, "text/plain", "stopped");
    });

    // POST /release-dmx — exit manual override; DMX position commands resume.
    server.on("/release-dmx", HTTP_POST, [](AsyncWebServerRequest* req) {
        orionReleaseToDmx();
        req->send(200, "text/plain", "released");
    });

    // POST /setlimit?which=down|up — capture current position as soft limit.
    // Persists config to NVS. Same operation that DMX function bytes 150-199/200-255 trigger.
    // Safety gate: refused while DMX is armed (Enable != 0).
    server.on("/setlimit", HTTP_POST, [](AsyncWebServerRequest* req) {
        IMotorDriver* drv = orionGetDriver();
        if (!drv) { req->send(503, "text/plain", "driver unavailable"); return; }
        if (dmxIsActive() && orionDmxLastEnable() != 0) {
            req->send(409, "text/plain", "DMX is armed — set Enable to 0 on the console first");
            return;
        }
        if (!req->hasParam("which", true)) {
            req->send(400, "text/plain", "missing which"); return;
        }
        String which = req->getParam("which", true)->value();
        int32_t pos = drv->getStatus().position;
        if (which == "down") {
            orionConfig.downPosition = pos;
        } else if (which == "up") {
            orionConfig.upPosition = pos;
        } else {
            req->send(400, "text/plain", "which must be up or down"); return;
        }
        orionApplySoftLimitsExternal();
        saveConfig();
        float cm = (float)pos / orionStepsPerCm();
        ESP_LOGI(TAG, "set %s limit via /setlimit: %d steps (%.1f cm)", which.c_str(), pos, cm);
        req->send(200, "text/plain", String(cm, 1));
    });

#ifdef ORION_HAS_LED
    // POST /ledhighlight?out=i — white-wipe identification on one LED output
    server.on("/ledhighlight", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (!req->hasParam("out", true)) { req->send(400, "text/plain", "missing out"); return; }
        orionHighlightLed(req->getParam("out", true)->value().toInt());
        req->send(200, "text/plain", "ok");
    });
#endif
}

#endif // RAVLIGHT_FIXTURE_ORION
