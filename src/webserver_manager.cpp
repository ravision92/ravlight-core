#include "webserver_manager.h"
#include "config.h"
#include "settings.h"
#include "network_manager.h"
#include <ElegantOTA.h>
#include <runtimeNVS.h>
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
#include "fixtures/veyron/dmx_fixture.h"
#include "fixtures/veyron/fixture_html.h"
#endif

#ifdef RAVLIGHT_MODULE_RECORDER
#include "dmx_recorder.h"
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
    f += "};</script>";
    return f;
}

void initWebServer() {
    if (!SPIFFS.begin(true)) {
        Serial.println("[WS] SPIFFS init failed");
        return;
    }

    // --- Static assets (shared) ---
    server.on("/Background.jpg", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(SPIFFS, "/Background.jpg", "image/jpeg");
    });
    server.on("/RavLightLogo.png", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(SPIFFS, "/RavLightLogo.png", "image/png");
    });
    server.on("/favicon.png", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(SPIFFS, "/favicon.png", "image/png");
    });
    server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(SPIFFS, "/style.css", "text/css");
    });

    // --- Root page ---
#ifdef RAVLIGHT_FIXTURE_VEYRON
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        String html = readFile(SPIFFS, "/index.html");
        // Inject feature flags and fixture section before other replacements
        html.replace("{{FEATURES}}", buildFeatureFlags());
        html.replace("{{FIXTURE_SECTION}}", VEYRON_FIXTURE_HTML);
        // Fixture-specific placeholders
        html.replace("{{rgbw_start_address}}",   String(dmxConfig.RGBWstartAddress));
        html.replace("{{strobe_start_address}}", String(dmxConfig.strobeStartAddress));
        html.replace("{{wh_start_address}}",     String(dmxConfig.WhStartAddress));
        html.replace("{{personality1_selected}}", dmxConfig.selectedPersonality == PERSONALITY_1 ? "selected" : "");
        html.replace("{{personality2_selected}}", dmxConfig.selectedPersonality == PERSONALITY_2 ? "selected" : "");
        html.replace("{{personality3_selected}}", dmxConfig.selectedPersonality == PERSONALITY_3 ? "selected" : "");
        html.replace("{{personality4_selected}}", dmxConfig.selectedPersonality == PERSONALITY_4 ? "selected" : "");
        html.replace("{{personality5_selected}}", dmxConfig.selectedPersonality == PERSONALITY_5 ? "selected" : "");
        // Shared placeholders
        html.replace("{{board_name}}",      BOARD_NAME);
        html.replace("{{ID_fixture}}",      setConfig.ID_fixture);
        html.replace("{{mdns_host}}",       "rav" + setConfig.ID_fixture + ".local");
        html.replace("{{show_ip_address}}", netConfig.currentip);
        html.replace("{{wifi_ssid}}",       netConfig.wifiSSID);
        html.replace("{{wifi_password}}",   netConfig.wifiPassword);
        html.replace("{{dhcp_checked}}",    netConfig.dhcp ? "checked" : "");
        html.replace("{{ip_address}}",      netConfig.ip);
        html.replace("{{subnet_mask}}",     netConfig.subnet);
        html.replace("{{gateway}}",         netConfig.gateway);
        html.replace("{{DMX_PHYSICAL}}",    dmxConfig.dmxInput == DMX_PHYSICAL ? "selected" : "");
        html.replace("{{ARTNET}}",          dmxConfig.dmxInput == ARTNET       ? "selected" : "");
        html.replace("{{SACN}}",            dmxConfig.dmxInput == SACN         ? "selected" : "");
        html.replace("{{AUTO_SCENE}}",      dmxConfig.dmxInput == AUTO_SCENE   ? "selected" : "");
        html.replace("{{dmx_output}}",      dmxConfig.dmxOutputEnabled ? "checked" : "");
        html.replace("{{start_universe}}",  String(dmxConfig.startUniverse));
        html.replace("{{LINEAR}}",          setConfig.DimCurves == LINEAR         ? "selected" : "");
        html.replace("{{SQUARE}}",          setConfig.DimCurves == SQUARE         ? "selected" : "");
        html.replace("{{INVERSE_SQUARE}}",  setConfig.DimCurves == INVERSE_SQUARE ? "selected" : "");
        html.replace("{{S_CURVE}}",         setConfig.DimCurves == S_CURVE        ? "selected" : "");
        html.replace("{{firmware_version}}", FW_VERSION);
        request->send(200, "text/html", html);
    });
#endif // RAVLIGHT_FIXTURE_VEYRON

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
        if (request->hasParam("personality", true)) {
            setPersonality(static_cast<FixturePersonality>(
                request->getParam("personality", true)->value().toInt()));
        }
        if (request->hasParam("RGBWstartAddress", true) ||
            request->hasParam("WhStartAddress", true) ||
            request->hasParam("strobeStartAddress", true)) {
            int rgbw   = request->hasParam("RGBWstartAddress", true)
                         ? request->getParam("RGBWstartAddress", true)->value().toInt()
                         : dmxConfig.RGBWstartAddress;
            int wh     = request->hasParam("WhStartAddress", true)
                         ? request->getParam("WhStartAddress", true)->value().toInt()
                         : dmxConfig.WhStartAddress;
            int strobe = request->hasParam("strobeStartAddress", true)
                         ? request->getParam("strobeStartAddress", true)->value().toInt()
                         : dmxConfig.strobeStartAddress;
            setFixtureAddresses(rgbw, wh, strobe);
        }
#endif
        if (request->hasParam("dimCurves", true)) {
            uint16_t v = request->getParam("dimCurves", true)->value().toInt();
#ifdef RAVLIGHT_FIXTURE_VEYRON
            setDimCurve(v);  // also updates fixture's cached dimcurve local var
#else
            setConfig.DimCurves = v;
#endif
        }

        // --- Restart-required params ---
        if (request->hasParam("ID_fixture", true)) {
            setConfig.ID_fixture = request->getParam("ID_fixture", true)->value();
            needsRestart = true;
        }
        if (request->hasParam("ssid", true)) {
            netConfig.wifiSSID = request->getParam("ssid", true)->value();
            needsRestart = true;
        }
        if (request->hasParam("password", true)) {
            netConfig.wifiPassword = request->getParam("password", true)->value();
            needsRestart = true;
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
            netConfig.subnet = request->getParam("subnet", true)->value();
            needsRestart = true;
        }
        if (request->hasParam("gateway", true)) {
            netConfig.gateway = request->getParam("gateway", true)->value();
            needsRestart = true;
        }

        saveConfig();

        DynamicJsonDocument resp(128);
        resp["restart"] = needsRestart;
        resp["newIp"]   = newIp;
        String respStr;
        serializeJson(resp, respStr);
        request->send(200, "application/json", respStr);

        if (needsRestart) {
            delay(300);
            ESP.restart();
        }
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

    // --- Fixture Veyron routes ---
#ifdef RAVLIGHT_FIXTURE_VEYRON
    server.on("/highlight", HTTP_POST, [](AsyncWebServerRequest *request) {
        startHighlight();
        request->send(200, "text/plain", "Highlight started");
    });
#endif

    // --- DMX Recorder module routes ---
#ifdef RAVLIGHT_MODULE_RECORDER
    server.on("/startRecording", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (request->hasParam("scene")) {
            int scene = request->getParam("scene")->value().toInt();
            startSceneRecording(scene);
            request->send(200, "text/plain", "Recording scene " + String(scene));
        } else {
            request->send(400, "text/plain", "Missing scene parameter");
        }
    });

    server.on("/playScene", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (request->hasParam("scene")) {
            int scene = request->getParam("scene")->value().toInt();
            playScene(scene);
            request->send(200, "text/plain", "Playing scene " + String(scene));
        } else {
            request->send(400, "text/plain", "Missing scene parameter");
        }
    });

    server.on("/listScenes", HTTP_GET, [](AsyncWebServerRequest *request) {
        String response = "Scenes:\n";
        for (int i = 0; i < 4; i++) {
            String fileName = "/scene_" + String(i) + ".bin";
            response += "Scene " + String(i) + (SPIFFS.exists(fileName) ? " available" : " empty") + "\n";
        }
        request->send(200, "text/plain", response);
    });
#endif // RAVLIGHT_MODULE_RECORDER

    // --- Config upload/download (shared) ---
    server.on("/upload_config", HTTP_POST,
        [](AsyncWebServerRequest *request) {},
        NULL,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            String json = String((char*)data).substring(0, len);
            File f = SPIFFS.open("/config.json", FILE_WRITE);
            if (f) { f.print(json); f.close(); }
            request->send(200, "text/plain", "OK");
            scheduleRestart();
        }
    );

    server.on("/download_config", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!SPIFFS.exists("/config.json")) {
            request->send(404, "text/plain", "Config file not found");
            return;
        }
        File configFile = SPIFFS.open("/config.json", FILE_READ);
        if (!configFile) {
            request->send(500, "text/plain", "Failed to open config");
            return;
        }
        DynamicJsonDocument doc(1024);
        DeserializationError error = deserializeJson(doc, configFile);
        configFile.close();
        if (error) {
            request->send(500, "text/plain", "Config parse error");
            return;
        }
        doc["board"]   = BOARD_NAME;
        doc["project"] = PROJECT_NAME;
        String output;
        serializeJsonPretty(doc, output);
        request->send(200, "application/json", output);
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
