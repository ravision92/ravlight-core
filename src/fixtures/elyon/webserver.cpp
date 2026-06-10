#ifdef RAVLIGHT_FIXTURE_ELYON
#include "fixture_webserver.h"
#include "fixtures/elyon/fixture.h"
#include "config.h"
#include "dmx_manager.h"
#include <string.h>

void handleFixtureSaveParams(AsyncWebServerRequest* request, bool& needsRestart) {
    bool changed         = false;
    bool needsHardRestart = false;  // protocol change or pixel count increase → RMT reinit

    // Pre-pass: enforce pixel budget AND backend caps. Reject the whole save if
    // any limit is exceeded (max 8 RMT WS281x outputs / max 8 I2S WS281x outputs,
    // dictated by the ESP32 classic 8 RMT channels and 8 I2S parallel lanes).
    // Clocked outputs run on bit-bang GPIO and don't consume an RMT/I2S slot.
    uint32_t totalPixels = 0;
    int rmt_used = 0;
    int i2s_used = 0;
    for (int i = 0; i < HW_LED_OUTPUT_COUNT; i++) {
        String protoKey = "elyonProto" + String(i);
        uint8_t proto = request->hasParam(protoKey, true)
                        ? (uint8_t)request->getParam(protoKey, true)->value().toInt()
                        : (uint8_t)elyonConfig.outputs[i].protocol;
        if (proto == (uint8_t)LED_PWM || proto == (uint8_t)LED_RELAY ||
            proto == (uint8_t)LED_CLOCK_FOLLOWER) continue;

        String countKey = "elyonCount" + String(i);
        uint32_t count = request->hasParam(countKey, true)
                         ? (uint32_t)request->getParam(countKey, true)->value().toInt()
                         : (uint32_t)elyonConfig.outputs[i].pixel_count;
        if (count > ELYON_MAX_PIXELS_PER_OUT) return;
        totalPixels += count;
        if (count == 0) continue;

        bool isClkProto = (proto == (uint8_t)LED_APA102 ||
                           proto == (uint8_t)LED_SK9822 ||
                           proto == (uint8_t)LED_P9813);
        if (isClkProto) continue;  // clocked: bit-bang, no RMT/I2S slot

#ifdef RAVLIGHT_MODULE_I2S_LED
        uint8_t backend = request->hasParam("elyonBackend" + String(i), true)
                          ? (uint8_t)request->getParam("elyonBackend" + String(i), true)->value().toInt()
                          : elyonConfig.outputs[i].backend;
        if (backend > 1) backend = (uint8_t)LED_BACKEND_RMT;
#else
        uint8_t backend = (uint8_t)LED_BACKEND_RMT;
#endif
        if (backend == (uint8_t)LED_BACKEND_I2S) i2s_used++;
        else                                     rmt_used++;
    }
    if (totalPixels > ELYON_MAX_PIXELS_TOTAL) return;
    if (rmt_used > 8) return;
    if (i2s_used > 8) return;

    for (int i = 0; i < HW_LED_OUTPUT_COUNT; i++) {
        led_output_cfg_t& o = elyonConfig.outputs[i];

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

        led_protocol_t newProto = (led_protocol_t)getU8("elyonProto", (uint8_t)o.protocol);
        uint16_t       newUniv  = getU16("elyonUniv", o.universe_start);
        uint16_t       newCh    = getU16("elyonCh",   o.dmx_start);
        uint8_t        newBri   = getU8 ("elyonBri",   o.brightness);
        if (newCh == 0) newCh = 1;

        // Any protocol type change requires RMT/LEDC/GPIO reinit
        if (newProto != o.protocol) needsHardRestart = true;

        if (newProto == LED_RELAY) {
            uint8_t newThr    = getU8("elyonRelayThr", o.relay_threshold);
            uint8_t newRelInv = request->hasParam("elyonRelayInv" + String(i), true) ? 1 : 0;
            if (newProto != o.protocol || newThr != o.relay_threshold ||
                newRelInv != o.relay_invert ||
                newUniv != o.universe_start || newCh != o.dmx_start) {
                o.protocol        = newProto;
                o.pixel_count     = 0;
                o.grouping        = 1;
                o.invert          = 0;
                o.pwm_freq_hz     = 0;
                o.pwm_curve       = 0;
                o.pwm_16bit       = 0;
                o.pwm_invert      = 0;
                o.relay_threshold = newThr;
                o.relay_invert    = newRelInv;
                o.universe_start  = newUniv;
                o.dmx_start       = newCh;
                changed = true;
            }
        } else if (newProto == LED_PWM) {
            uint16_t newFreq   = getU16("elyonPwmFreq", o.pwm_freq_hz ? o.pwm_freq_hz : 1000);
            uint8_t  newCurve  = getU8 ("elyonCurve",   o.pwm_curve);
            uint8_t  newBit    = request->hasParam("elyonPwm16" + String(i), true) ? 1 : 0;
            uint8_t  newPwmInv = request->hasParam("elyonPwmInv" + String(i), true) ? 1 : 0;

            if (newProto != o.protocol || newFreq != o.pwm_freq_hz || newCurve != o.pwm_curve ||
                newBit != o.pwm_16bit || newPwmInv != o.pwm_invert || newBri != o.brightness ||
                newUniv != o.universe_start || newCh != o.dmx_start) {
                o.protocol       = newProto;
                o.pixel_count    = 0;
                o.grouping       = 1;
                o.invert         = 0;
                o.pwm_freq_hz    = newFreq;
                o.pwm_curve      = newCurve;
                o.pwm_16bit      = newBit;
                o.pwm_invert     = newPwmInv;
                o.brightness     = newBri;
                o.universe_start = newUniv;
                o.dmx_start      = newCh;
                changed = true;
            }
        } else if (newProto == LED_CLOCK_FOLLOWER) {
            // Follower outputs aren't directly user-configured — the per-card form
            // for a follower only submits the protocol (hidden input). Leave the
            // other fields untouched. The post-loop consistency pass below will
            // synchronise the follower state with whichever clocked output claims
            // it, or restore it to a sensible default if unclaimed.
            if (newProto != o.protocol) {
                o.protocol = newProto;
                changed = true;
            }
        } else {
            uint16_t newCount = getU16("elyonCount", o.pixel_count);
            // Any pixel count change requires RMT reinit: increase would overflow the buffer,
            // decrease would leave strips[i].n_pixels stale and drive extra pixels with garbage.
            if (newCount != o.pixel_count) needsHardRestart = true;
            uint8_t  newGroup = getU8 ("elyonGroup",  o.grouping);
            uint8_t  newInv   = request->hasParam("elyonInv" + String(i), true) ? 1 : 0;
            if (newGroup == 0) newGroup = 1;

            uint8_t newOrder[4] = {o.color_order[0], o.color_order[1],
                                   o.color_order[2], o.color_order[3]};
            String orderKey = "elyonOrder" + String(i);
            if (request->hasParam(orderKey, true)) {
                String v = request->getParam(orderKey, true)->value();
                v.toUpperCase();
                uint8_t ch_pp = led_ch_per_pixel(newProto);
                if ((int)v.length() == ch_pp) color_order_from_str(v.c_str(), newOrder);
            }

            bool orderChanged = (newOrder[0] != o.color_order[0] || newOrder[1] != o.color_order[1] ||
                                 newOrder[2] != o.color_order[2] || newOrder[3] != o.color_order[3]);

            // Clocked output (APA102 / SK9822 / P9813): read the CLOCK partner index.
            // Validation: partner must be in range and != self. Invalid choices fall
            // back to LED_WS2812B (the safest neutral default).
            uint8_t newPartner = o.clock_partner_idx;
            if (led_is_clocked(newProto)) {
                newPartner = getU8("elyonClockPartner", newPartner);
                if (newPartner == (uint8_t)i || newPartner >= HW_LED_OUTPUT_COUNT) {
                    ESP_LOGW("ELYON_WEB",
                             "ch%d clocked: invalid clock partner %u — falling back to WS2812B",
                             i, newPartner);
                    newProto   = LED_WS2812B;
                    newPartner = 0xFF;
                }
            } else {
                newPartner = 0xFF;  // not clocked → clear any stale partner index
            }

            bool partnerChanged = (newPartner != o.clock_partner_idx);

            // Backend: clocked protocols always use bit-bang (treated as RMT slot
            // for cap accounting but driver is clocked_output). WS281x outputs
            // honour the user's pick. Invalid/missing → keep current value.
            // RMT-only builds ignore the field at runtime; we still store it so
            // round-trip is clean if the same config is reloaded on an I2S build.
            uint8_t newBackend = o.backend;
#ifdef RAVLIGHT_MODULE_I2S_LED
            if (request->hasParam("elyonBackend" + String(i), true)) {
                newBackend = (uint8_t)request->getParam("elyonBackend" + String(i), true)->value().toInt();
                if (newBackend > 1) newBackend = (uint8_t)LED_BACKEND_RMT;
            }
            if (led_is_clocked(newProto)) newBackend = (uint8_t)LED_BACKEND_RMT;
#endif
            bool backendChanged = (newBackend != o.backend);

            if (newProto != o.protocol || newCount != o.pixel_count ||
                newUniv != o.universe_start || newCh != o.dmx_start ||
                newGroup != o.grouping || newInv != o.invert || newBri != o.brightness ||
                orderChanged || partnerChanged || backendChanged) {
                o.protocol         = newProto;
                o.pixel_count      = newCount;
                o.universe_start   = newUniv;
                o.dmx_start        = newCh;
                o.grouping         = newGroup;
                o.invert           = newInv;
                o.brightness       = newBri;
                o.clock_partner_idx = newPartner;
                o.backend          = newBackend;
                memcpy(o.color_order, newOrder, 4);
                o.pwm_freq_hz      = 0;
                o.pwm_curve        = 0;
                o.pwm_16bit        = 0;
                changed = true;
                if (partnerChanged || backendChanged) needsHardRestart = true;
            }
        }
    }

    // ── Consistency pass for clocked outputs ────────────────────────────────
    // After processing all outputs, reconcile the FOLLOWER state with whichever
    // clocked outputs currently claim it. Iterated to handle cascades where
    // converting an output to FOLLOWER frees one of its previously-claimed pins.
    for (int iter = 0; iter < HW_LED_OUTPUT_COUNT; iter++) {
        bool claimed[HW_LED_OUTPUT_COUNT] = {false};
        for (int i = 0; i < HW_LED_OUTPUT_COUNT; i++) {
            const led_output_cfg_t& o = elyonConfig.outputs[i];
            if (led_is_clocked(o.protocol) &&
                o.clock_partner_idx < HW_LED_OUTPUT_COUNT &&
                o.clock_partner_idx != (uint8_t)i) {
                claimed[o.clock_partner_idx] = true;
            }
        }
        bool stable = true;
        for (int i = 0; i < HW_LED_OUTPUT_COUNT; i++) {
            led_output_cfg_t& o = elyonConfig.outputs[i];
            if (claimed[i] && o.protocol != LED_CLOCK_FOLLOWER) {
                // Reserve as CLOCK line — wipe the output's own DMX/render config
                o.protocol          = LED_CLOCK_FOLLOWER;
                o.pixel_count       = 0;
                o.grouping          = 1;
                o.invert            = 0;
                o.brightness        = 0;
                o.pwm_freq_hz       = 0;
                o.pwm_curve         = 0;
                o.pwm_16bit         = 0;
                o.pwm_invert        = 0;
                o.relay_threshold   = 128;
                o.relay_invert      = 0;
                o.clock_partner_idx = 0xFF;
                changed = true;
                needsHardRestart = true;
                stable = false;
            } else if (!claimed[i] && o.protocol == LED_CLOCK_FOLLOWER) {
                // No longer claimed — return to a safe neutral default
                o.protocol          = LED_WS2812B;
                o.pixel_count       = 0;
                o.brightness        = 255;
                o.clock_partner_idx = 0xFF;
                changed = true;
                needsHardRestart = true;
                stable = false;
            }
        }
        if (stable) break;
    }

    if (changed) {
        saveConfig();
        if (needsHardRestart) {
            needsRestart = true;
        } else {
            // Live update: register any new universes without restart.
            // Old universe pool entries are harmless (pool has 32 slots).
            for (int i = 0; i < HW_LED_OUTPUT_COUNT; i++) {
                const led_output_cfg_t& o = elyonConfig.outputs[i];
                uint8_t n = led_universe_count(&o);
                for (uint8_t u = 0; u < n; u++)
                    registerDmxUniverse(o.universe_start + u);
            }
        }
    }
}

extern void elyonHighlightOutput(int idx);

void registerFixtureRoutes(AsyncWebServer& server) {
    // POST /ledhighlight?out=i — white-wipe identification on one output
    server.on("/ledhighlight", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (!req->hasParam("out", true)) { req->send(400, "text/plain", "missing out"); return; }
        elyonHighlightOutput(req->getParam("out", true)->value().toInt());
        req->send(200, "text/plain", "ok");
    });
}

#endif // RAVLIGHT_FIXTURE_ELYON
