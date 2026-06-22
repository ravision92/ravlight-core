#ifdef RAVLIGHT_FIXTURE_AXON
#include "fixture_webserver.h"
#include "fixtures/axon/fixture.h"
#include "config.h"
#include "dmx_manager.h"
#include <string.h>

void handleFixtureSaveParams(AsyncWebServerRequest* request, bool& needsRestart) {
#ifdef AXON_HAS_LED
    bool changed = false;
    bool needsHardRestart = false;

    for (int i = 0; i < AXON_NUM_LED_OUTPUTS; i++) {
        led_output_cfg_t& o = axonConfig.ledOutputs[i];

        auto getU16 = [&](const char* base, uint16_t fallback) -> uint16_t {
            String k = String(base) + String(i);
            return request->hasParam(k, true)
                   ? (uint16_t)request->getParam(k, true)->value().toInt()
                   : fallback;
        };
        auto getU8 = [&](const char* base, uint8_t fallback) -> uint8_t {
            String k = String(base) + String(i);
            return request->hasParam(k, true)
                   ? (uint8_t)request->getParam(k, true)->value().toInt()
                   : fallback;
        };

        led_protocol_t newProto = (led_protocol_t)getU8("axonProto", (uint8_t)o.protocol);
        uint16_t       newUniv  = getU16("axonUniv", o.universe_start);
        uint16_t       newCh    = getU16("axonCh",   o.dmx_start);
        uint16_t       newCount = getU16("axonCount", o.pixel_count);
        uint8_t        newGroup = getU8 ("axonGroup", o.grouping);
        uint8_t        newInv   = getU8 ("axonInv",   o.invert);
        uint8_t        newBri   = getU8 ("axonBri",   o.brightness);
        uint8_t        newGamma = getU8 ("axonGamma", o.gamma_x10);
        if (newCh == 0) newCh = 1;
        if (newGroup == 0) newGroup = 1;
        if (newGamma < 10) newGamma = 10;
        if (newGamma > 30) newGamma = 30;
#ifdef RAVLIGHT_MODULE_I2S_LED
        uint8_t        newBackend = getU8("axonBackend", o.backend);
        if (newBackend > 1) newBackend = (uint8_t)LED_BACKEND_RMT;
#else
        uint8_t        newBackend = (uint8_t)LED_BACKEND_RMT;
#endif

        if (newProto != o.protocol)            needsHardRestart = true;
        if (newCount != o.pixel_count)         needsHardRestart = true;
        if (newBackend != o.backend)           needsHardRestart = true;

        uint8_t newOrder[4] = {0, 1, 2, 3};
        String orderKey = "axonOrder" + String(i);
        if (request->hasParam(orderKey, true)) {
            color_order_from_str(request->getParam(orderKey, true)->value().c_str(), newOrder);
        } else {
            memcpy(newOrder, o.color_order, 4);
        }

        if (newProto == LED_PWM) {
            uint16_t newFreq  = getU16("axonPwmFreq",  o.pwm_freq_hz);
            uint8_t  newCurve = getU8 ("axonPwmCurve", o.pwm_curve);
            uint8_t  new16bit = getU8 ("axonPwm16",    o.pwm_16bit);
            uint8_t  newPwmInv= getU8 ("axonPwmInv",   o.pwm_invert);
            if (o.protocol != newProto || o.pwm_freq_hz != newFreq ||
                o.pwm_16bit != new16bit) needsHardRestart = true;
            o.protocol     = newProto;
            o.pwm_freq_hz  = newFreq;
            o.pwm_curve    = newCurve;
            o.pwm_16bit    = new16bit;
            o.pwm_invert   = newPwmInv;
            o.brightness   = newBri;
            changed = true;
        } else if (newProto == LED_RELAY) {
            uint8_t newThr = getU8("axonRelayThr", o.relay_threshold);
            uint8_t newRInv= getU8("axonRelayInv", o.relay_invert);
            o.protocol        = newProto;
            o.relay_threshold = newThr;
            o.relay_invert    = newRInv;
            o.universe_start  = newUniv;
            o.dmx_start       = newCh;
            changed = true;
        } else {
            // WS281x / clocked
            o.protocol       = newProto;
            o.pixel_count    = newCount;
            o.universe_start = newUniv;
            o.dmx_start      = newCh;
            o.grouping       = newGroup;
            o.invert         = newInv;
            o.brightness     = newBri;
            o.gamma_x10      = newGamma;
            o.backend        = newBackend;
            memcpy(o.color_order, newOrder, 4);
            o.pwm_freq_hz    = 0;
            changed = true;
        }
    }

    if (changed) {
        saveConfig();
        if (needsHardRestart) {
            needsRestart = true;
        } else {
            // Live update — register new universes; no driver re-init.
            for (int i = 0; i < AXON_NUM_LED_OUTPUTS; i++) {
                const led_output_cfg_t& o = axonConfig.ledOutputs[i];
                uint8_t n = led_universe_count(&o);
                for (uint8_t u = 0; u < n; u++)
                    registerDmxUniverse(o.universe_start + u);
            }
        }
    }
#else
    (void)request; (void)needsRestart;
#endif
}

void registerFixtureRoutes(AsyncWebServer& server) {
    // POST /highlight — flash all configured LED outputs briefly so the
    // operator can identify which physical fixture this firmware lives on.
    server.on("/highlight", HTTP_POST, [](AsyncWebServerRequest* req) {
        extern void fixtureHighlight();
        fixtureHighlight();
        req->send(200, "text/plain", "ok");
    });

#ifdef AXON_HAS_LED
    // POST /ledhighlight?out=i — wipe one LED output (for the per-row
    // identify button in the Axon UI).
    server.on("/ledhighlight", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (!req->hasParam("out", true)) { req->send(400, "text/plain", "missing out"); return; }
        axonHighlightLed(req->getParam("out", true)->value().toInt());
        req->send(200, "text/plain", "ok");
    });
#endif
}

#endif // RAVLIGHT_FIXTURE_AXON
