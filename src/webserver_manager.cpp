#include "webserver_manager.h"
#include "config.h"
#include "fixture_config.h"
#include <LittleFS.h>
#include "network_manager.h"
#include <ElegantOTA.h>
#include "runtime.h"
#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <Ticker.h>
#include "dmx_manager.h"
#include <memory>
#include <WiFi.h>

#ifdef RAVLIGHT_MODULE_ETHERNET
#include <ETH.h>
#endif

#ifdef RAVLIGHT_MODULE_TEMP
#include "temp_sensor.h"
#endif

#include "fixture_webserver.h"

#ifdef RAVLIGHT_MODULE_RECORDER
#include "dmx_recorder.h"
#endif
#ifdef RAVLIGHT_MODULE_DISCOVERY
#include "discovery_shared.h"
#include "discovery_udp.h"
#include "discovery_espnow.h"
static Ticker espnowScanTicker;
static Ticker espnowCmdTicker;
static Ticker espnowCmdRestoreTicker;
static struct {
    char hwMac[18];
    char cmd[12];
    char ssid[64];
    char pwd[64];
} s_pendingCmd;
#endif

extern uint32_t totalRuntime;
extern uint32_t currentRuntime;
extern int16_t WiFiScanStatus;

#ifdef RAVLIGHT_MODULE_TEMP
extern float SensTemp;
#endif

static AsyncWebServer server(80);
extern String ScannedSSID;
Ticker restartTimer;

AsyncWebServer& getInstance() {
    return server;
}

// ── Single-pass template engine ───────────────────────────────────────────────
// Streams directly from a LittleFS File into `out` (pre-reserved String).
// State machine detects {{...}} placeholders without loading the full template
// into RAM — peak allocation is just the output String, not template + output.
static String buildFeatureFlags();  // forward declaration — defined below

static void writeHTMLVar(String& out, const char* var) {
    char b[12];  // scratch buffer for numeric conversions
    // ── Shared placeholders ───────────────────────────────────────────────────
    if      (strcmp(var, "FEATURES")        == 0) { String f = buildFeatureFlags(); out.concat(f); }
    else if (strcmp(var, "connection_mode") == 0) { out.concat(getConnectionMode()); }
    else if (strcmp(var, "board_name")      == 0) { out.concat(BOARD_NAME); }
    else if (strcmp(var, "ID_fixture")      == 0) { out.concat(setConfig.ID_fixture.c_str()); }
    else if (strcmp(var, "mdns_host")       == 0) { out.concat("rav"); out.concat(setConfig.ID_fixture.c_str()); out.concat(".local"); }
    else if (strcmp(var, "show_ip_address") == 0) { out.concat(netConfig.currentip.c_str()); }
    else if (strcmp(var, "wifi_ssid")       == 0) { out.concat(netConfig.wifiSSID.c_str()); }
    else if (strcmp(var, "wifi_password")   == 0) { out.concat(netConfig.wifiPassword.c_str()); }
    else if (strcmp(var, "dhcp_checked")    == 0) { if (netConfig.dhcp) out.concat("checked"); }
    else if (strcmp(var, "ip_address")      == 0) { out.concat(netConfig.ip.c_str()); }
    else if (strcmp(var, "subnet_mask")     == 0) { out.concat(netConfig.subnet.c_str()); }
    else if (strcmp(var, "gateway")         == 0) { out.concat(netConfig.gateway.c_str()); }
    else if (strcmp(var, "DMX_PHYSICAL")    == 0) { if (dmxConfig.dmxInput == DMX_PHYSICAL) out.concat("selected"); }
    else if (strcmp(var, "ARTNET")          == 0) { if (dmxConfig.dmxInput == ARTNET)        out.concat("selected"); }
    else if (strcmp(var, "SACN")            == 0) { if (dmxConfig.dmxInput == SACN)          out.concat("selected"); }
    else if (strcmp(var, "AUTO_SCENE")      == 0) { if (dmxConfig.dmxInput == AUTO_SCENE)    out.concat("selected"); }
    else if (strcmp(var, "dmx_output")      == 0) { if (dmxConfig.dmxOutputEnabled) out.concat("checked"); }
    else if (strcmp(var, "start_universe")  == 0) { snprintf(b, sizeof(b), "%u", (unsigned)dmxConfig.startUniverse); out.concat(b); }
    else if (strcmp(var, "firmware_version")== 0) { out.concat("FW " FW_VERSION); }
    else if (strncmp(var, "scene_slot_sel_", 15) == 0) {
        int slot = atoi(var + 15);
        if (dmxConfig.autoSceneSlot == slot) out.concat("selected");
    }
    // ── Fixture-specific ─────────────────────────────────────────────────────
    else writeFixtureVars(out, var);
}

static void writeHTMLFromFile(String& out, File& file) {
    char readBuf[512];  // larger buffer → fewer LittleFS reads
    char varBuf[64];
    int  varLen = 0;
    enum { NORMAL, SAW_OPEN1, IN_VAR, SAW_CLOSE1 } state = NORMAL;

    while (file.available()) {
        int nr = file.read((uint8_t*)readBuf, sizeof(readBuf));
        if (nr <= 0) break;

        int batchStart = 0;  // start of unflushed NORMAL run in this buffer

        for (int i = 0; i < nr; i++) {
            char c = readBuf[i];
            switch (state) {
            case NORMAL:
                if (c == '{') {
                    // flush the normal run up to (but not including) this '{'
                    if (i > batchStart) out.concat(readBuf + batchStart, i - batchStart);
                    state = SAW_OPEN1;
                }
                break;
            case SAW_OPEN1:
                if (c == '{') { state = IN_VAR; varLen = 0; }
                else { out += '{'; out += c; state = NORMAL; batchStart = i + 1; }
                break;
            case IN_VAR:
                if (c == '}') state = SAW_CLOSE1;
                else if (varLen < (int)sizeof(varBuf) - 1) varBuf[varLen++] = c;
                break;
            case SAW_CLOSE1:
                if (c == '}') {
                    varBuf[varLen] = '\0';
                    writeHTMLVar(out, varBuf);
                    state = NORMAL; varLen = 0; batchStart = i + 1;
                } else {
                    out.concat("{{"); out.concat(varBuf, varLen); out += '}'; out += c;
                    state = NORMAL; varLen = 0; batchStart = i + 1;
                }
                break;
            }
        }
        // flush remaining NORMAL chars in this buffer
        if (state == NORMAL && batchStart < nr)
            out.concat(readBuf + batchStart, nr - batchStart);
    }
    if (state == SAW_OPEN1) out += '{';
    else if (state == IN_VAR)     { out.concat("{{"); out.concat(varBuf, varLen); }
    else if (state == SAW_CLOSE1) { out.concat("{{"); out.concat(varBuf, varLen); out += '}'; }
}

// ─────────────────────────────────────────────────────────────────────────────

// Build compile-time feature flags JS object injected into the HTML template
static String buildFeatureFlags() {
    String f = "<script>const F={";
    f += "dmx:1,";
#ifdef RAVLIGHT_MODULE_DMX_PHYSICAL
    f += "dmxPhysical:1,";
#endif
#ifdef RAVLIGHT_MODULE_RECORDER
    f += "recorder:1,";
#endif
#ifdef RAVLIGHT_MODULE_TEMP
    f += "temp:1,";
#endif
#ifdef RAVLIGHT_MODULE_ETHERNET
    f += "ethernet:1,";
#endif
    f += "fixture:\"" PROJECT_NAME "\",";
#ifdef RAVLIGHT_MODULE_DISCOVERY
    f += "discovery:1,";
#endif
#ifdef RAVLIGHT_MODULE_I2S_LED
    f += "i2s:1,";
#endif
    f += "};</script>";
    return f;
}

// Serve a text asset preferring its pre-gzipped sibling when present. The
// scripts/gzip_assets.py pre-build hook produces <path>.gz next to each
// .html/.css/.js asset; here we transparently pick the smaller copy and
// advertise Content-Encoding: gzip so the browser inflates on the fly.
static void sendAsset(AsyncWebServerRequest *request,
                      const char *path, const char *contentType) {
    String gz = String(path) + ".gz";
    if (LittleFS.exists(gz)) {
        AsyncWebServerResponse *r = request->beginResponse(LittleFS, gz, contentType);
        r->addHeader("Content-Encoding", "gzip");
        request->send(r);
    } else {
        request->send(LittleFS, path, contentType);
    }
}

void initWebServer() {
    if (!LittleFS.begin(false)) {
        // Do not auto-format: a SPIFFS partition from older firmware must be re-flashed
        // via uploadfs, not silently wiped. Server still starts to allow OTA recovery.
        Serial.println("[WS] LittleFS mount failed — upload filesystem via /update");
    }

    // --- Static assets (shared) ---
    server.on("/Background.jpg", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(LittleFS, "/Background.jpg", "image/jpeg");
    });
    server.on("/RavLightLogo.png", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(LittleFS, "/RavLightLogo.png", "image/png");
    });
    server.on("/favicon.png", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(LittleFS, "/favicon.png", "image/png");
    });
    server.on("/Iicon.png", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(LittleFS, "/Iicon.png", "image/png");
    });
    server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request) {
        sendAsset(request, "/style.css", "text/css");
    });
    server.on("/dmxmonitor", HTTP_GET, [](AsyncWebServerRequest *request) {
        sendAsset(request, "/dmxmonitor.html", "text/html");
    });

    // --- Root page (SPA shell — static) ---
    // index.html is now a small (~6 KB) SPA shell that loads app.js + fixture.js
    // and fetches /api/{features,status,config} for state. No server-side template
    // engine — page rendering is the client's job, the firmware just streams the
    // file. Old chunked handler (writeHTMLFromFile state machine) is retained
    // below for reference but unused; will be removed in the cleanup commit.
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (LittleFS.exists("/index.html") || LittleFS.exists("/index.html.gz")) {
            sendAsset(request, "/index.html", "text/html");
        } else {
            request->send(200, "text/html",
                "<html><body><h2>RavLight FW " FW_VERSION "</h2>"
                "<p>Web UI not found — upload filesystem via "
                "<a href='/update'>OTA</a> (select Filesystem).</p>"
                "</body></html>");
        }
    });
    server.on("/app.js", HTTP_GET, [](AsyncWebServerRequest *request) {
        sendAsset(request, "/app.js", "application/javascript");
    });
    server.on("/output-card.js", HTTP_GET, [](AsyncWebServerRequest *request) {
        sendAsset(request, "/output-card.js", "application/javascript");
    });
    server.on("/fixture.js", HTTP_GET, [](AsyncWebServerRequest *request) {
#ifdef RAVLIGHT_FIXTURE_ELYON
        sendAsset(request, "/elyon/fixture.js", "application/javascript");
#elif defined(RAVLIGHT_FIXTURE_VEYRON)
        sendAsset(request, "/veyron/fixture.js", "application/javascript");
#elif defined(RAVLIGHT_FIXTURE_ORION)
        sendAsset(request, "/orion/fixture.js", "application/javascript");
#else
        request->send(404, "text/plain", "fixture js missing");
#endif
    });

    // Legacy chunked-template handler — kept compiled out but referenced below
    // until the cleanup commit removes writeHTMLFromFile / writeHTMLVar etc.
    server.on("/__legacy_index", HTTP_GET, [](AsyncWebServerRequest *request) {
        struct TmplState {
            File file;
            enum State { NORMAL, SAW_OPEN1, IN_VAR, SAW_CLOSE1 };
            State state = NORMAL;
            char varBuf[64];
            int  varLen = 0;
            String pending;     // overflow from placeholder expansion or fallback emits
            size_t pendingPos = 0;
            bool eofHandled = false;
        };
        auto ps = std::shared_ptr<TmplState>(new (std::nothrow) TmplState);
        if (!ps) { request->send(503, "text/plain", "OOM"); return; }
        ps->file = LittleFS.open("/index.html", "r");
        if (!ps->file) {
            request->send(200, "text/html",
                "<html><body><h2>RavLight FW " FW_VERSION "</h2>"
                "<p>Web UI not found — upload filesystem via "
                "<a href='/update'>OTA</a> (select Filesystem).</p>"
                "</body></html>");
            return;
        }
        Serial.printf("[WS] GET / streaming freeHeap=%u\n", ESP.getFreeHeap());

        auto response = request->beginChunkedResponse("text/html",
            [ps](uint8_t* buf, size_t maxLen, size_t /*idx*/) -> size_t {
                size_t outPos = 0;
                while (outPos < maxLen) {
                    // 1. Drain pending placeholder/fallback output
                    if (ps->pendingPos < ps->pending.length()) {
                        size_t avail = ps->pending.length() - ps->pendingPos;
                        size_t want  = maxLen - outPos;
                        size_t take  = (avail < want) ? avail : want;
                        memcpy(buf + outPos, ps->pending.c_str() + ps->pendingPos, take);
                        ps->pendingPos += take;
                        outPos += take;
                        if (ps->pendingPos >= ps->pending.length()) {
                            ps->pending = "";
                            ps->pendingPos = 0;
                        }
                        continue;
                    }
                    // 2. EOF handling — flush dangling state then finish
                    if (!ps->file.available()) {
                        if (ps->eofHandled) {
                            ps->file.close();
                            return outPos;   // 0 here signals end-of-stream
                        }
                        ps->eofHandled = true;
                        if (ps->state == TmplState::SAW_OPEN1) {
                            ps->pending = "{";
                        } else if (ps->state == TmplState::IN_VAR) {
                            ps->pending = "{{";
                            ps->pending.concat(ps->varBuf, ps->varLen);
                        } else if (ps->state == TmplState::SAW_CLOSE1) {
                            ps->pending = "{{";
                            ps->pending.concat(ps->varBuf, ps->varLen);
                            ps->pending += '}';
                        }
                        ps->state = TmplState::NORMAL;
                        ps->varLen = 0;
                        continue;
                    }
                    // 3. Read one char from file (LittleFS has internal buffering)
                    int ic = ps->file.read();
                    if (ic < 0) { ps->eofHandled = true; continue; }
                    char c = (char)ic;
                    switch (ps->state) {
                    case TmplState::NORMAL:
                        if (c == '{') ps->state = TmplState::SAW_OPEN1;
                        else          buf[outPos++] = c;
                        break;
                    case TmplState::SAW_OPEN1:
                        if (c == '{') { ps->state = TmplState::IN_VAR; ps->varLen = 0; }
                        else {
                            buf[outPos++] = '{';
                            if (outPos < maxLen) buf[outPos++] = c;
                            else { ps->pending = ""; ps->pending += c; }
                            ps->state = TmplState::NORMAL;
                        }
                        break;
                    case TmplState::IN_VAR:
                        if (c == '}') ps->state = TmplState::SAW_CLOSE1;
                        else if (ps->varLen < (int)sizeof(ps->varBuf) - 1)
                            ps->varBuf[ps->varLen++] = c;
                        break;
                    case TmplState::SAW_CLOSE1:
                        if (c == '}') {
                            ps->varBuf[ps->varLen] = '\0';
                            ps->pending = "";
                            writeHTMLVar(ps->pending, ps->varBuf);
                            ps->state = TmplState::NORMAL;
                            ps->varLen = 0;
                        } else {
                            ps->pending = "{{";
                            ps->pending.concat(ps->varBuf, ps->varLen);
                            ps->pending += '}';
                            ps->pending += c;
                            ps->state = TmplState::NORMAL;
                            ps->varLen = 0;
                        }
                        break;
                    }
                }
                return outPos;
            });
        request->send(response);
    });

    // --- WiFi scan (shared) ---
    server.on("/scanWiFi", HTTP_GET, [](AsyncWebServerRequest *request) {
        scanWiFiNetworks();
        request->send(200, "text/plain", "Scanning");
    });
    server.on("/getWiFiList", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", requestWiFiNetworks());
    });

    // --- Unified smart save ---
    // Applies live params immediately; restarts only if network/ID params changed.
    // Response: JSON {"restart": bool, "newIp": "x.x.x.x"}
    server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request) {
        bool needsRestart = false;
        String newIp = netConfig.currentip;

        // --- Live params: apply immediately, no restart ---
        if (request->hasParam("autoSceneSlot", true)) {
            uint8_t v = (uint8_t)request->getParam("autoSceneSlot", true)->value().toInt();
            if (v != dmxConfig.autoSceneSlot) {
                dmxConfig.autoSceneSlot = v;
#ifdef RAVLIGHT_MODULE_RECORDER
                if (dmxConfig.dmxInput == AUTO_SCENE) {
                    stopAutoScene();
                    startAutoScene(dmxConfig.autoSceneSlot);
                }
#endif
            }
        }
        if (request->hasParam("dmxInput", true)) {
            uint16_t v = request->getParam("dmxInput", true)->value().toInt();
            if (v != dmxConfig.dmxInput) {
                dmxConfig.dmxInput = v;
                reinitDMXInput();
            }
        }
        if (request->hasParam("startUniverse", true)) {
            uint16_t v = request->getParam("startUniverse", true)->value().toInt();
            if (v != dmxConfig.startUniverse) {
                dmxConfig.startUniverse = v;
                reinitUniverse(v);
            }
        }
        if (request->hasParam("dmxOutput", true) != dmxConfig.dmxOutputEnabled) {
            bool v = request->hasParam("dmxOutput", true);
            dmxConfig.dmxOutputEnabled = v;
            reinitDMXOutput(v);
        }
        handleFixtureSaveParams(request, needsRestart);

        // --- Restart-required params (restart only when value actually changed) ---
        if (request->hasParam("ID_fixture", true)) {
            String v = request->getParam("ID_fixture", true)->value();
            if (v != setConfig.ID_fixture) { setConfig.ID_fixture = v; needsRestart = true; }
        }
        if (request->hasParam("ssid", true)) {
            String v = request->getParam("ssid", true)->value();
            if (v != netConfig.wifiSSID) { netConfig.wifiSSID = v; needsRestart = true; }
        }
        if (request->hasParam("password", true)) {
            String v = request->getParam("password", true)->value();
            if (v != netConfig.wifiPassword) { netConfig.wifiPassword = v; needsRestart = true; }
        }
        bool newDhcp = request->hasParam("dhcp", true);
        if (newDhcp != netConfig.dhcp) {
            netConfig.dhcp = newDhcp;
            needsRestart = true;
        }
        if (request->hasParam("ip", true)) {
            String v = request->getParam("ip", true)->value();
            if (v != netConfig.ip) {
                netConfig.ip = v;
                if (!netConfig.dhcp) newIp = v;
                needsRestart = true;
            }
        }
        if (request->hasParam("subnet", true)) {
            String v = request->getParam("subnet", true)->value();
            if (v != netConfig.subnet) { netConfig.subnet = v; needsRestart = true; }
        }
        if (request->hasParam("gateway", true)) {
            String v = request->getParam("gateway", true)->value();
            if (v != netConfig.gateway) { netConfig.gateway = v; needsRestart = true; }
        }

        saveConfig();

        DynamicJsonDocument resp(192);
        resp["restart"]  = needsRestart;
        resp["newIp"]    = newIp;
        resp["mdnsHost"] = "rav" + setConfig.ID_fixture + ".local";
        resp["connType"] = getConnectionMode();
        String respStr;
        serializeJson(resp, respStr);
        request->send(200, "application/json", respStr);

        if (needsRestart)
            scheduleRestart();
    });

    // --- Reset to defaults ---
    server.on("/reset", HTTP_POST, [](AsyncWebServerRequest *request) {
        resetConfig();
        request->send(200, "text/plain", "Configuration reset to defaults.");
        scheduleRestart();
    });
    // --- Reset to defaults ---
    server.on("/restart", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", "Restarting...");
        scheduleRestart();
    });
    // --- Runtime (shared) ---
    server.on("/runtime", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", String(currentRuntime));
    });
    server.on("/truntime", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", String(totalRuntime));
    });

    // --- Temperature sensor module ---
#ifdef RAVLIGHT_MODULE_TEMP
    server.on("/temperature", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", String(SensTemp));
    });
#endif

    // --- DMX activity status (shared by all fixtures + UI header chip) ---
    server.on("/dmxstatus", HTTP_GET, [](AsyncWebServerRequest *request) {
        String json = String("{\"active\":") + (dmxIsActive() ? "true" : "false") + "}";
        request->send(200, "application/json", json);
    });

    // --- DMX Recorder module routes ---
#ifdef RAVLIGHT_MODULE_RECORDER
    server.on("/startRecording", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (request->hasParam("scene")) {
            int scene = request->getParam("scene")->value().toInt();
            startSceneRecording(scene);
            request->send(200, "text/plain", "Recording scene " + String(scene + 1));
        } else {
            request->send(400, "text/plain", "Missing scene parameter");
        }
    });

    server.on("/recordingStatus", HTTP_GET, [](AsyncWebServerRequest *request) {
        String json = isRecording ? "{\"recording\":true}" : "{\"recording\":false}";
        request->send(200, "application/json", json);
    });

    server.on("/listScenes", HTTP_GET, [](AsyncWebServerRequest *request) {
        String response = "Scenes:\n";
        for (int i = 0; i < 4; i++) {
            String fileName = "/scene_" + String(i) + ".bin";
            response += "Scene " + String(i) + (LittleFS.exists(fileName) ? " available" : " empty") + "\n";
        }
        request->send(200, "text/plain", response);
    });
#endif // RAVLIGHT_MODULE_RECORDER

    // --- Config upload/download (shared) ---
    // Upload: parse JSON directly into in-memory structs → save to NVS → restart
    server.on("/upload_config", HTTP_POST,
        [](AsyncWebServerRequest *request) {},
        NULL,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            DynamicJsonDocument doc(4096);
            if (deserializeJson(doc, (char*)data, len)) {
                request->send(400, "text/plain", "JSON parse error");
                return;
            }
            applyConfigJson(doc);
            saveConfig();
            request->send(200, "text/plain", "OK");
            scheduleRestart();
        }
    );

    // Download: serialize from in-memory structs (always up-to-date with NVS)
    server.on("/download_config", HTTP_GET, [](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(4096);
        doc["version"] = 2;
        doc["board"]   = BOARD_NAME;
        doc["project"] = PROJECT_NAME;
        doc["ID_fixture"] = setConfig.ID_fixture;

        JsonObject net = doc.createNestedObject("network");
        net["id"]       = setConfig.ID_fixture;
        net["ssid"]     = netConfig.wifiSSID;
        net["password"] = netConfig.wifiPassword;
        net["dhcp"]     = netConfig.dhcp;
        net["ip"]       = netConfig.ip;
        net["subnet"]   = netConfig.subnet;
        net["gateway"]  = netConfig.gateway;

        JsonObject dmx = doc.createNestedObject("dmx");
        dmx["input"]    = dmxConfig.dmxInput;
        dmx["universe"] = dmxConfig.startUniverse;
#ifdef RAVLIGHT_MODULE_DMX_PHYSICAL
        dmx["output"]   = dmxConfig.dmxOutputEnabled;
#endif

        JsonObject fix = doc.createNestedObject("fixture");
        fixtureConfigSerialize(fix);

        String output;
        serializeJsonPretty(doc, output);
        request->send(200, "application/json", output);
    });

    // ─── JSON API for the SPA frontend ──────────────────────────────────────
    // Same shape as /download_config but smaller (compact, not pretty) and
    // intended for client-side rendering.
    server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(4096);
        doc["version"]    = 2;
        doc["board"]      = BOARD_NAME;
        doc["project"]    = PROJECT_NAME;
        doc["ID_fixture"] = setConfig.ID_fixture;

        JsonObject net = doc.createNestedObject("network");
        net["id"]       = setConfig.ID_fixture;
        net["ssid"]     = netConfig.wifiSSID;
        net["password"] = netConfig.wifiPassword;
        net["dhcp"]     = netConfig.dhcp;
        net["ip"]       = netConfig.ip;
        net["subnet"]   = netConfig.subnet;
        net["gateway"]  = netConfig.gateway;

        JsonObject dmx = doc.createNestedObject("dmx");
        dmx["input"]    = dmxConfig.dmxInput;
        dmx["universe"] = dmxConfig.startUniverse;
#ifdef RAVLIGHT_MODULE_DMX_PHYSICAL
        dmx["output"]   = dmxConfig.dmxOutputEnabled;
#endif
#ifdef RAVLIGHT_MODULE_RECORDER
        dmx["autoSceneSlot"] = dmxConfig.autoSceneSlot;
#endif

        JsonObject fix = doc.createNestedObject("fixture");
        fixtureConfigSerialize(fix);

        String out;
        serializeJson(doc, out);
        request->send(200, "application/json", out);
    });

    // Runtime info — what the SPA shows in the header / status panel.
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(512);
        doc["fw"]            = FW_VERSION;
        doc["board"]         = BOARD_NAME;
        doc["project"]       = PROJECT_NAME;
        doc["id"]            = setConfig.ID_fixture;
        doc["mode"]          = getConnectionMode();
        doc["ip"]            = netConfig.currentip;
        doc["mac"]           = WiFi.macAddress();
        doc["runtime"]       = currentRuntime;
        doc["total_runtime"] = totalRuntime;
        doc["heap_free"]     = ESP.getFreeHeap();
        doc["heap_min"]      = ESP.getMinFreeHeap();
#ifdef RAVLIGHT_MODULE_TEMP
        doc["temp"] = SensTemp;
#endif
        doc["dmx_active"] = dmxIsActive();
        String out;
        serializeJson(doc, out);
        request->send(200, "application/json", out);
    });

    // Compile-time feature flags (formerly emitted inline as <script>const F={...}>).
    server.on("/api/features", HTTP_GET, [](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(256);
        doc["dmx"] = 1;
#ifdef RAVLIGHT_MODULE_DMX_PHYSICAL
        doc["dmxPhysical"] = 1;
#endif
#ifdef RAVLIGHT_MODULE_RECORDER
        doc["recorder"] = 1;
#endif
#ifdef RAVLIGHT_MODULE_TEMP
        doc["temp"] = 1;
#endif
#ifdef RAVLIGHT_MODULE_ETHERNET
        doc["ethernet"] = 1;
#endif
#ifdef RAVLIGHT_MODULE_DISCOVERY
        doc["discovery"] = 1;
#endif
#ifdef RAVLIGHT_MODULE_I2S_LED
        doc["i2s"] = 1;
#endif
        doc["fixture"]    = PROJECT_NAME;
        doc["hw_outputs"] = HW_LED_OUTPUT_COUNT;
        JsonArray pins = doc.createNestedArray("hw_pins");
        for (int i = 0; i < HW_LED_OUTPUT_COUNT; i++) pins.add(HW_LED_OUTPUT_PINS[i]);
        String out;
        serializeJson(doc, out);
        request->send(200, "application/json", out);
    });

    // POST /api/config — apply a JSON config blob and persist to NVS. Validates
    // before commit; returns {ok, restart_needed} so the client can decide whether
    // to POST /restart afterwards. Existing form-based /save endpoint remains for
    // backward compatibility while the SPA is rolled in.
    AsyncCallbackJsonWebHandler* postConfig = new AsyncCallbackJsonWebHandler(
        "/api/config",
        [](AsyncWebServerRequest *request, JsonVariant &body) {
            if (!body.is<JsonObject>()) {
                request->send(400, "application/json",
                              "{\"ok\":false,\"error\":\"invalid json\"}");
                return;
            }
            JsonObject doc = body.as<JsonObject>();
            bool restartNeeded = false;

            // ── Network section ────────────────────────────────────────────
            if (doc.containsKey("network")) {
                JsonObject net = doc["network"].as<JsonObject>();
                String newSsid    = net["ssid"]     | netConfig.wifiSSID;
                String newPwd     = net["password"] | netConfig.wifiPassword;
                bool   newDhcp    = net["dhcp"]     | netConfig.dhcp;
                String newIp      = net["ip"]       | netConfig.ip;
                String newSubnet  = net["subnet"]   | netConfig.subnet;
                String newGateway = net["gateway"]  | netConfig.gateway;
                if (newSsid != netConfig.wifiSSID ||
                    newPwd  != netConfig.wifiPassword ||
                    newDhcp != netConfig.dhcp ||
                    newIp   != netConfig.ip ||
                    newSubnet  != netConfig.subnet ||
                    newGateway != netConfig.gateway) {
                    netConfig.wifiSSID     = newSsid;
                    netConfig.wifiPassword = newPwd;
                    netConfig.dhcp         = newDhcp;
                    netConfig.ip           = newIp;
                    netConfig.subnet       = newSubnet;
                    netConfig.gateway      = newGateway;
                    restartNeeded = true;
                }
            }

            // ── ID_fixture (mDNS hostname / AP SSID) ───────────────────────
            if (doc.containsKey("ID_fixture")) {
                String newId = doc["ID_fixture"] | setConfig.ID_fixture;
                if (newId != setConfig.ID_fixture && newId.length() > 0) {
                    setConfig.ID_fixture = newId;
                    restartNeeded = true;
                }
            }

            // ── DMX section ────────────────────────────────────────────────
            if (doc.containsKey("dmx")) {
                JsonObject dmx = doc["dmx"].as<JsonObject>();
                uint8_t  newInput = dmx["input"]    | (uint8_t)dmxConfig.dmxInput;
                uint16_t newUniv  = dmx["universe"] | dmxConfig.startUniverse;
                if (newInput != dmxConfig.dmxInput || newUniv != dmxConfig.startUniverse) {
                    dmxConfig.dmxInput     = newInput;
                    dmxConfig.startUniverse = newUniv;
                    restartNeeded = true;
                }
#ifdef RAVLIGHT_MODULE_DMX_PHYSICAL
                bool newOut = dmx["output"] | dmxConfig.dmxOutputEnabled;
                if (newOut != dmxConfig.dmxOutputEnabled) {
                    dmxConfig.dmxOutputEnabled = newOut;
                    restartNeeded = true;
                }
#endif
#ifdef RAVLIGHT_MODULE_RECORDER
                uint8_t newSlot = dmx["autoSceneSlot"] | dmxConfig.autoSceneSlot;
                if (newSlot != dmxConfig.autoSceneSlot) {
                    dmxConfig.autoSceneSlot = newSlot;
                }
#endif
            }

            // ── Fixture section ────────────────────────────────────────────
            // Delegated to fixtureConfigDeserialize() which already understands
            // the on-disk JSON shape (outputs[]). Validation responsibility
            // currently sits in handleFixtureSaveParams's form path — for the SPA
            // path we trust the client and rely on saveConfig() not crashing on
            // bad values. A dedicated fixtureValidateJson() is a TODO.
            if (doc.containsKey("fixture")) {
                JsonObject fix = doc["fixture"].as<JsonObject>();
                fixtureConfigDeserialize(fix);
                restartNeeded = true;  // pixel count / protocol changes need re-init
            }

            saveConfig();

            DynamicJsonDocument resp(128);
            resp["ok"]             = true;
            resp["restart_needed"] = restartNeeded;
            String out;
            serializeJson(resp, out);
            request->send(200, "application/json", out);
        });
    server.addHandler(postConfig);

    registerFixtureRoutes(server);

#ifdef RAVLIGHT_MODULE_DISCOVERY
    // Trigger a new UDP discovery scan (fire-and-forget — client polls /devices after delay)
    server.on("/discover", HTTP_GET, [](AsyncWebServerRequest *request) {
        bool withESPNow    = request->hasParam("espnow") &&
                             request->getParam("espnow")->value() == "1";
        bool wifiDisrupted = withESPNow && strcmp(getConnectionMode(), "WiFi") == 0;

        startCombinedDiscovery(withESPNow);

        char resp[80];
        snprintf(resp, sizeof(resp),
                 "{\"scanning\":true,\"duration\":%d,\"wifiDisrupted\":%s}",
                 DISC_SCAN_TOTAL_MS, wifiDisrupted ? "true" : "false");
        request->send(200, "application/json", resp);

        if (wifiDisrupted) {
            // Defer WiFi disconnect + ESP-NOW broadcast by 200 ms so the HTTP
            // response above has time to be delivered before WiFi drops.
            espnowScanTicker.once_ms(200, []() {
                suspendWiFiSTA();
                startESPNowDiscovery();
                triggerESPNowScanStart();
            });
        }
    });

    // Return current ScannedDevices as JSON array
    server.on("/devices", HTTP_GET, [](AsyncWebServerRequest *request) {
        const auto& devices = getDiscoveredUDPDevices();
        DynamicJsonDocument doc(2048);
        JsonArray arr = doc.to<JsonArray>();
        for (const auto& d : devices) {
            JsonObject obj = arr.createNestedObject();
            obj["fixture"] = d.fixture;
            obj["id"]      = d.id;
            obj["mode"]    = d.mode;
            obj["ip"]      = d.ip;
            obj["mac"]     = d.mac;
            obj["hwMac"]   = d.hwMac;   // non-empty → device was found via ESP-NOW
            obj["fw"]      = d.fw;
            obj["temp"]    = d.temp;
            obj["uptime"]  = d.uptime;
        }
        String resp;
        serializeJson(doc, resp);
        request->send(200, "application/json", resp);
    });

    // Send a command to a device: routes via ESP-NOW if hwmac is present, UDP otherwise
    server.on("/device-cmd", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!request->hasParam("ip", true) || !request->hasParam("command", true)) {
            request->send(400, "text/plain", "Missing ip or command");
            return;
        }
        String ip      = request->getParam("ip",      true)->value();
        String command = request->getParam("command", true)->value();
        String ssid, password;
        if (request->hasParam("ssid",     true)) ssid     = request->getParam("ssid",     true)->value();
        if (request->hasParam("password", true)) password = request->getParam("password", true)->value();
        // Device discovered via ESP-NOW: route command back via ESP-NOW
        if (request->hasParam("hwmac", true) && !request->getParam("hwmac", true)->value().isEmpty()) {
            String hwMac = request->getParam("hwmac", true)->value();
            if (strcmp(getConnectionMode(), "WiFi") == 0) {
                // WiFi STA active — home channel = router channel, not AP_CHANNEL.
                // Defer: respond first, then suspend WiFi, send command, restore WiFi.
                strlcpy(s_pendingCmd.hwMac, hwMac.c_str(),    sizeof(s_pendingCmd.hwMac));
                strlcpy(s_pendingCmd.cmd,   command.c_str(),  sizeof(s_pendingCmd.cmd));
                strlcpy(s_pendingCmd.ssid,  ssid.c_str(),     sizeof(s_pendingCmd.ssid));
                strlcpy(s_pendingCmd.pwd,   password.c_str(), sizeof(s_pendingCmd.pwd));
                request->send(200, "text/plain", "queued");
                espnowCmdTicker.once_ms(200, []() {
                    suspendWiFiSTA();
                    sendESPNowCommand(s_pendingCmd.hwMac, s_pendingCmd.cmd,
                                      s_pendingCmd.ssid,  s_pendingCmd.pwd);
                    espnowCmdRestoreTicker.once_ms(500, resumeWiFiSTA);
                });
            } else {
                bool ok = sendESPNowCommand(hwMac, command, ssid, password);
                request->send(200, "text/plain", ok ? "sent" : "failed");
            }
            return;
        }
        // Device discovered via UDP: route command via UDP
        IPAddress target;
        if (!target.fromString(ip)) {
            request->send(400, "text/plain", "Invalid IP");
            return;
        }
        bool ok = sendUDPCommand(target, command, ssid, password);
        request->send(200, "text/plain", ok ? "sent" : "failed");
    });
#endif

    // --- DMX Monitor data endpoints ---
    server.on("/dmxdata", HTTP_GET, [](AsyncWebServerRequest *req) {
        uint16_t universe = 0;
        if (req->hasParam("universe")) universe = (uint16_t)req->getParam("universe")->value().toInt();

        uint8_t local[513] = {0};
        xSemaphoreTake(dmxBufferMutex, portMAX_DELAY);
        const uint8_t* buf = getUniverseData(universe);
        if (buf) memcpy(local + 1, buf + 1, 512);
        xSemaphoreGive(dmxBufferMutex);

        String out;
        out.reserve(2560);
        out += '[';
        for (int i = 1; i <= 512; i++) {
            out += local[i];
            if (i < 512) out += ',';
        }
        out += ']';
        req->send(200, "application/json", out);
    });

    server.on("/dmxmap", HTTP_GET, [](AsyncWebServerRequest *req) {
        DynamicJsonDocument doc(2048);
        JsonObject map = doc.createNestedObject("map");
        fixtureGetDmxMap(map);
        String respStr;
        serializeJson(doc, respStr);
        req->send(200, "application/json", respStr);
    });

    server.begin();

    ElegantOTA.begin(&server);
    ElegantOTA.onStart([]() {
        Serial.println("[WS] OTA update started");
        stopDMX();
    });
}

String readFile(fs::FS &fs, const char *path) {
    Serial.printf("[WS] Reading file: %s\n", path);
    File file = fs.open(path, "r");
    if (!file || file.isDirectory()) {
        Serial.println("[WS] Failed to open file");
        return String();
    }
    String content;
    content.reserve(file.size());   // single allocation — avoids repeated realloc+copy
    while (file.available()) content += (char)file.read();
    file.close();
    return content;
}

String requestWiFiNetworks() {
    return ScannedSSID;
}

void startWebServer() {
    server.begin();
}

void stopWebServer() {
    server.end();
}

void scheduleRestart() {
    restartTimer.once(2, []() {
        ESP.restart();
    });
}



