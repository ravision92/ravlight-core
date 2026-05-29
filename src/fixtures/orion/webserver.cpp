#ifdef RAVLIGHT_FIXTURE_ORION
#include "fixture_webserver.h"
#include "fixtures/orion/fixture.h"
#include "fixtures/orion/fixture_html.h"
#include "core/motor/IMotorDriver.h"
#include "config.h"
#include "dmx_manager.h"
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <esp_log.h>

static const char* TAG = "ORION_WEB";

// ── HTML / JS injection ─────────────────────────────────────────────────────

#ifdef ORION_HAS_LED
// One LED output as a collapsible ch-card (same pattern as the motor sub-sections
// and Elyon). Pixel protocols only (WS281x); enable an output by setting count > 0.
static void appendOrionLedCard(String& out, int i) {
    const led_output_cfg_t& o = orionConfig.ledOutputs[i];
    char n[12], iS[4];
    snprintf(iS, sizeof(iS), "%d", i);
    char order_str[5];
    color_order_to_str(o.color_order, led_ch_per_pixel(o.protocol), order_str);
    const char* protoName = (o.protocol == LED_SK6812) ? "SK6812 RGBW" :
                            (o.protocol == LED_WS2814) ? "WS2814 RGBW" :
                            (o.protocol == LED_WS2811) ? "WS2811" : "WS2812B";

    // ── Card head (id + summary) ─────────────────────────────────────────────
    out += "<div class=\"ch-card\">";
    out += "<div class=\"ch-head\" onclick=\"orionToggle(this)\">";
    out += "<div class=\"ch-id\"><span class=\"ch-num\">LED"; snprintf(n,sizeof(n),"%d",i+1); out += n; out += "</span>";
    out += "<span class=\"ch-gpio\">GPIO "; snprintf(n,sizeof(n),"%d",HW_LED_OUTPUT_PINS[i]); out += n; out += "</span></div>";
    out += "<div class=\"ch-sum\"><span class=\"ch-proto\" id=\"oLedProto"; out += iS; out += "\">"; out += protoName; out += "</span>";
    out += "<div class=\"ch-tags\">";
    out += "<span class=\"ch-tag\" id=\"oLedPx"; out += iS; out += "\"><b>"; snprintf(n,sizeof(n),"%u",(unsigned)o.pixel_count); out += n; out += "</b> px</span>";
    out += "<span class=\"ch-tag\">U:<b id=\"oLedU"; out += iS; out += "\">"; snprintf(n,sizeof(n),"%u",(unsigned)o.universe_start); out += n; out += "</b></span>";
    out += "<span class=\"ch-tag\">CH:<b id=\"oLedCh"; out += iS; out += "\">"; snprintf(n,sizeof(n),"%u",(unsigned)o.dmx_start); out += n; out += "</b></span>";
    out += "</div></div>";
    out += "<span class=\"ch-arr\">&#9661;</span></div>";

    // ── Card body (form) ─────────────────────────────────────────────────────
    out += "<div class=\"ch-body\"><div class=\"ch-form\">";

    out += "<div class=\"field\"><label class=\"lbl\">Protocol</label>";
    out += "<select name=\"orionLedProto"; out += iS; out += "\" id=\"oLedProtoSel"; out += iS;
    out += "\" onchange=\"orionLedSum("; out += iS; out += ")\">";
    out += "<option value=\"0\"";  if (o.protocol==LED_WS2811)  out+=" selected"; out+=">WS2811</option>";
    out += "<option value=\"1\"";  if (o.protocol==LED_WS2812B) out+=" selected"; out+=">WS2812B</option>";
    out += "<option value=\"2\"";  if (o.protocol==LED_SK6812)  out+=" selected"; out+=">SK6812 RGBW</option>";
    out += "<option value=\"3\"";  if (o.protocol==LED_WS2814)  out+=" selected"; out+=">WS2814 RGBW</option>";
    out += "</select></div>";

    out += "<div class=\"g2\">";
    out += "<div class=\"field\"><label class=\"lbl\">Pixel count</label>";
    out += "<input type=\"number\" name=\"orionLedCount"; out += iS; out += "\" id=\"oLedCount"; out += iS;
    out += "\" min=\"0\" max=\"1024\" value=\""; snprintf(n,sizeof(n),"%u",(unsigned)o.pixel_count); out += n;
    out += "\" oninput=\"orionLedSum("; out += iS; out += ")\"></div>";
    out += "<div class=\"field\"><label class=\"lbl\">Color order</label>";
    out += "<input type=\"text\" name=\"orionLedOrder"; out += iS; out += "\" maxlength=\"4\" value=\"";
    out += order_str; out += "\" oninput=\"this.value=this.value.toUpperCase()\" style=\"text-align:center;letter-spacing:.1em\"></div>";
    out += "</div>";

    out += "<div class=\"g2\">";
    out += "<div class=\"field\"><label class=\"lbl\">Universe</label>";
    out += "<input type=\"number\" name=\"orionLedUniv"; out += iS; out += "\" id=\"oLedUniv"; out += iS;
    out += "\" min=\"0\" max=\"32767\" value=\""; snprintf(n,sizeof(n),"%u",(unsigned)o.universe_start); out += n;
    out += "\" oninput=\"orionLedSum("; out += iS; out += ")\"></div>";
    out += "<div class=\"field\"><label class=\"lbl\">Start CH</label>";
    out += "<input type=\"number\" name=\"orionLedCh"; out += iS; out += "\" id=\"oLedChI"; out += iS;
    out += "\" min=\"1\" max=\"512\" value=\""; snprintf(n,sizeof(n),"%u",(unsigned)o.dmx_start); out += n;
    out += "\" oninput=\"orionLedSum("; out += iS; out += ")\"></div>";
    out += "</div>";

    out += "<div class=\"g2\">";
    out += "<div class=\"field\"><label class=\"lbl\">Brightness</label>";
    out += "<input type=\"number\" name=\"orionLedBri"; out += iS;
    out += "\" min=\"0\" max=\"255\" value=\""; snprintf(n,sizeof(n),"%u",(unsigned)o.brightness); out += n; out += "\"></div>";
    out += "<div class=\"field\"><label class=\"lbl\">Group</label>";
    out += "<input type=\"number\" name=\"orionLedGroup"; out += iS;
    out += "\" min=\"1\" max=\"255\" value=\""; snprintf(n,sizeof(n),"%u",(unsigned)o.grouping); out += n; out += "\"></div>";
    out += "</div>";

    out += "<div class=\"tog-row\"><input type=\"checkbox\" name=\"orionLedInv"; out += iS;
    out += "\""; if (o.invert) out += " checked"; out += "><span class=\"tog-lbl\">Invert signal</span></div>";

    out += "<button type=\"button\" class=\"act-btn\" style=\"margin-top:10px;border-radius:var(--r);padding:9px;font-size:12px\" onclick=\"orionLedHighlight("; out += iS; out += ")\">Highlight (white wipe)</button>";

    out += "</div></div></div>";  // ch-form, ch-body, ch-card
}

// Second fixture tab content — the LED outputs, wrapped like a tab section.
static void buildOrionLedSection(String& out) {
    out.reserve(out.length() + 6000);
    out += "<div class=\"acc-wrap\"><div class=\"acc-body open\"><div class=\"acc-inner\">";
    out += "<p class=\"field-note\">WS281x strips driven alongside the motor. Pixel count &gt; 0 enables an output. Changing pixel count or protocol restarts the device.</p>";
    out += "<div class=\"ch-list\">";
    for (int i = 0; i < HW_LED_OUTPUT_COUNT; i++) appendOrionLedCard(out, i);
    out += "</div></div></div></div>";
}
#endif // ORION_HAS_LED

static void buildOrionSection(String& out) {
    out.reserve(out.length() + 14000);
    out += ORION_FIXTURE_HTML;

    // Server-side token substitution for form field values.
    // Step values are converted to cm with one decimal — UI is cm-native.
    auto stepsToCm = [](long steps) -> String {
        return String((float)steps / orionStepsPerCm(), 1);
    };

    out.replace("{V_POSITION_START}", String(orionConfig.positionStart));
    out.replace("{V_CONTROL_START}",  String(orionConfig.controlStart));
    out.replace("{V_DOWN_CM}",        stepsToCm(orionConfig.downPosition));
    out.replace("{V_UP_CM}",          stepsToCm(orionConfig.upPosition));
    out.replace("{V_MAX_SPEED_CM}",   stepsToCm(orionConfig.maxSpeed));
    out.replace("{V_MAX_ACCEL_CM}",   stepsToCm(orionConfig.maxAccel));
    out.replace("{V_JOG_SPEED_CM}",   stepsToCm(orionConfig.jogSpeed));
    out.replace("{V_DRUM_MM}",        String(orionConfig.drumDiameterMm));
    out.replace("{V_GEAR}",           String(orionConfig.gearRatio, 2));
    out.replace("{V_STEPS_REV}",      String(orionConfig.motorStepsPerRev));
    out.replace("{V_STEPS_PER_CM}",   String(orionStepsPerCm(), 1));

    out.replace("{SEL_P1}", orionConfig.personality == 1 ? "selected" : "");
    out.replace("{SEL_P2}", orionConfig.personality == 2 ? "selected" : "");
    out.replace("{SEL_P3}", orionConfig.personality == 3 ? "selected" : "");
    out.replace("{SEL_UP}", orionConfig.homingDirection >= 0 ? "selected" : "");
    out.replace("{SEL_DN}", orionConfig.homingDirection <  0 ? "selected" : "");
    out.replace("{SEL_WDE}", orionConfig.dmxWatchdogAction == 0 ? "selected" : "");
    out.replace("{SEL_WDH}", orionConfig.dmxWatchdogAction == 1 ? "selected" : "");
}

void writeFixtureVars(String& out, const char* var) {
    if (strcmp(var, "FIXTURE_SECTION") == 0) {
        buildOrionSection(out);
    } else if (strcmp(var, "FIXTURE_SECTION_2") == 0) {
#ifdef ORION_HAS_LED
        buildOrionLedSection(out);   // second tab → LED outputs
#endif
    } else if (strcmp(var, "FIXTURE_JS") == 0) {
        out.concat(ORION_FIXTURE_JS);
    } else if (strcmp(var, "fixture_display_name") == 0) {
        out.concat(ORION_FIXTURE_NAME);
    } else if (strcmp(var, "fixture_tab1_name") == 0) {
#ifdef ORION_HAS_LED
        out.concat("Motor");
#else
        out.concat(ORION_FIXTURE_NAME);
#endif
    } else if (strcmp(var, "fixture_tab2_name") == 0) {
#ifdef ORION_HAS_LED
        out.concat("LED");
#endif
    } else if (strcmp(var, "dmx_universe_note") == 0) {
#ifdef ORION_HAS_LED
        out.concat("Start Universe addresses the motor. Each LED output has its own universe, set per output in the LED tab.");
#else
        out.concat("Universe for the motor DMX channels.");
#endif
    }
}

void injectFixturePlaceholders(String& html) {
    String section;
    buildOrionSection(section);
    html.replace("{{FIXTURE_SECTION}}", section);
    html.replace("{{FIXTURE_JS}}",      ORION_FIXTURE_JS);
#ifdef ORION_HAS_LED
    String led;
    buildOrionLedSection(led);
    html.replace("{{FIXTURE_SECTION_2}}", led);
#endif
}

// ── /save params — fixture-specific form fields ─────────────────────────────

static int32_t paramInt(AsyncWebServerRequest* req, const char* name, int32_t def) {
    if (req->hasParam(name, true)) return req->getParam(name, true)->value().toInt();
    return def;
}

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
        drv->jog(d, (float)orionConfig.jogSpeed);
        orionEnterManualOverride();   // suppress DMX motion until /release-dmx
        req->send(200, "text/plain", "jogging");
    });

    // POST /jogstop — end the current jog burst. Stays in manual override
    // until /release-dmx is called explicitly.
    server.on("/jogstop", HTTP_POST, [](AsyncWebServerRequest* req) {
        IMotorDriver* drv = orionGetDriver();
        if (!drv) { req->send(503, "text/plain", "driver unavailable"); return; }
        drv->stop();
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
