#include "webserver_manager.h"
#include "config.h"
#include "fixture_config.h"
#include <LittleFS.h>
#include "network_manager.h"
#include <ElegantOTA.h>
#include "runtime.h"
#include <ArduinoJson.h>
#include <Ticker.h>
#include "dmx_manager.h"

#ifdef RAVLIGHT_MODULE_ETHERNET
#include <ETH.h>
#endif

#ifdef RAVLIGHT_MODULE_TEMP
#include "temp_sensor.h"
#endif

#ifdef RAVLIGHT_FIXTURE_VEYRON
#include "fixtures/veyron/webserver.h"
#endif
#ifdef RAVLIGHT_FIXTURE_ELYON
#include "fixtures/elyon/webserver.h"
#endif

#ifdef RAVLIGHT_MODULE_RECORDER
#include "dmx_recorder.h"
#endif
#ifdef RAVLIGHT_MODULE_DISCOVERY
#include "discovery_shared.h"
#include "discovery_udp.h"
#include "discovery_espnow.h"
static Ticker espnowScanTicker;
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
#ifdef RAVLIGHT_FIXTURE_VEYRON
    f += "fixtureVeyron:1,";
#endif
#ifdef RAVLIGHT_FIXTURE_ELYON
    f += "fixtureElyon:1,";
#endif
#ifdef RAVLIGHT_MODULE_DISCOVERY
    f += "discovery:1,";
#endif
    f += "};</script>";
    return f;
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
        request->send(LittleFS, "/style.css", "text/css");
    });
    server.on("/dmxmonitor", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(LittleFS, "/dmxmonitor.html", "text/html");
    });

    // --- Root page ---
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        String html = readFile(LittleFS, "/index.html");
        if (html.isEmpty()) {
            request->send(200, "text/html",
                "<html><body><h2>RavLight FW " FW_VERSION "</h2>"
                "<p>Web UI not found. Upload the filesystem via "
                "<a href='/update'>OTA update</a> (select Filesystem).</p>"
                "</body></html>");
            return;
        }
        // Pre-allocate capacity for all placeholder expansions in one shot.
        // Without this, each String::replace() that grows the string triggers a
        // realloc — heap fragmentation from RMT buffers can prevent finding a
        // contiguous block, causing replace() to silently no-op (placeholder left raw).
        html.reserve(html.length() + 24000);
        html.replace("{{FEATURES}}", buildFeatureFlags());
        // Fixture-specific injection (section HTML, JS, and fixture placeholders)
#ifdef RAVLIGHT_FIXTURE_VEYRON
        injectVeyronPlaceholders(html);
#elif defined(RAVLIGHT_FIXTURE_ELYON)
        injectElyonPlaceholders(html);
#else
        html.replace("{{FIXTURE_SECTION}}",      "");
        html.replace("{{FIXTURE_JS}}",           "");
        html.replace("{{fixture_display_name}}", "");
#endif
        // Shared placeholders
        html.replace("{{connection_mode}}", getConnectionMode());
        html.replace("{{board_name}}",       BOARD_NAME);
        html.replace("{{ID_fixture}}",       setConfig.ID_fixture);
        html.replace("{{mdns_host}}",        "rav" + setConfig.ID_fixture + ".local");
        html.replace("{{show_ip_address}}",  netConfig.currentip);
        html.replace("{{wifi_ssid}}",        netConfig.wifiSSID);
        html.replace("{{wifi_password}}",    netConfig.wifiPassword);
        html.replace("{{dhcp_checked}}",     netConfig.dhcp ? "checked" : "");
        html.replace("{{ip_address}}",       netConfig.ip);
        html.replace("{{subnet_mask}}",      netConfig.subnet);
        html.replace("{{gateway}}",          netConfig.gateway);
        html.replace("{{DMX_PHYSICAL}}",     dmxConfig.dmxInput == DMX_PHYSICAL ? "selected" : "");
        html.replace("{{ARTNET}}",           dmxConfig.dmxInput == ARTNET       ? "selected" : "");
        html.replace("{{SACN}}",             dmxConfig.dmxInput == SACN         ? "selected" : "");
        html.replace("{{AUTO_SCENE}}",       dmxConfig.dmxInput == AUTO_SCENE   ? "selected" : "");
        html.replace("{{dmx_output}}",       dmxConfig.dmxOutputEnabled ? "checked" : "");
        html.replace("{{start_universe}}",   String(dmxConfig.startUniverse));
        for (int i = 0; i < 4; i++) {
            String ph = "{{scene_slot_sel_" + String(i) + "}}";
            html.replace(ph, dmxConfig.autoSceneSlot == i ? "selected" : "");
        }
        html.replace("{{firmware_version}}", "FW " FW_VERSION);
        request->send(200, "text/html", html);
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
#ifdef RAVLIGHT_FIXTURE_VEYRON
        handleVeyronSaveParams(request, needsRestart);
#endif
#ifdef RAVLIGHT_FIXTURE_ELYON
        handleElyonSaveParams(request, needsRestart);
#endif

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
        dmx["output"]   = dmxConfig.dmxOutputEnabled;
        dmx["dimCurve"] = setConfig.DimCurves;

        JsonObject fix = doc.createNestedObject("fixture");
        fixtureConfigSerialize(fix);

        String output;
        serializeJsonPretty(doc, output);
        request->send(200, "application/json", output);
    });

#ifdef RAVLIGHT_FIXTURE_VEYRON
    registerVeyronRoutes(server);
#endif
#ifdef RAVLIGHT_FIXTURE_ELYON
    registerElyonRoutes(server);
#endif

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
            obj["id"]     = d.id;
            obj["ip"]     = d.ip;
            obj["mac"]    = d.mac;
            obj["hwMac"]  = d.hwMac;   // non-empty → device was found via ESP-NOW
            obj["mode"]   = d.mode;
            obj["fw"]     = d.fw;
            obj["temp"]   = d.temp;
            obj["uptime"] = d.uptime;
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
            bool ok = sendESPNowCommand(hwMac, command, ssid, password);
            request->send(200, "text/plain", ok ? "sent" : "failed");
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
#ifdef RAVLIGHT_FIXTURE_VEYRON
        stopDMX();
#endif
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
