#ifdef RAVLIGHT_FIXTURE_ELYON
#include "fixture_webserver.h"
#include "fixtures/elyon/fixture_html.h"
#include "fixtures/elyon/fixture.h"
#include "config.h"
#include <string.h>

static void appendElyonCard(String& out, int i);  // forward declaration — defined below

void writeFixtureVars(String& out, const char* var) {
    if (strcmp(var, "FIXTURE_SECTION") == 0) {
        static const char tpl[] = ELYON_FIXTURE_HTML;
        static const char ph[]  = "{{ELYON_ROWS}}";
        const char* split = strstr(tpl, ph);
        if (split) {
            out.concat((const char*)tpl, split - tpl);
            for (int i = 0; i < HW_LED_OUTPUT_COUNT; i++) appendElyonCard(out, i);
            out.concat(split + sizeof(ph) - 1);
        } else {
            out.concat(tpl);
        }
    } else if (strcmp(var, "FIXTURE_JS") == 0) {
        out.concat(ELYON_FIXTURE_JS);
    } else if (strcmp(var, "fixture_display_name") == 0) {
        out.concat(ELYON_FIXTURE_NAME);
    } else if (strcmp(var, "ELYON_ROWS") == 0) {
        for (int i = 0; i < HW_LED_OUTPUT_COUNT; i++) appendElyonCard(out, i);
    }
}

// Appends a card-based output block directly into `out` (no intermediate String allocation).
// Uses ch-card/ch-head/ch-body CSS from index.html; server-renders current config values.
static void appendElyonCard(String& out, int i) {
    const elyon_output_cfg_t& o = elyonConfig.outputs[i];
    int gpio = (i < HW_LED_OUTPUT_COUNT) ? HW_LED_OUTPUT_PINS[i] : -1;
    bool isPwm   = (o.protocol == LED_PWM);
    bool isRelay = (o.protocol == LED_RELAY);
    bool isPx    = !isPwm && !isRelay;

    char order_str[5];
    color_order_to_str(o.color_order,
                       isPx ? led_ch_per_pixel(o.protocol) : 3, order_str);

    const char* protoName = isPwm   ? "PWM Dimmer" :
                            isRelay ? "Relay" :
                            (o.protocol == LED_SK6812) ? "SK6812 RGBW" :
                            (o.protocol == LED_WS2814) ? "WS2814 RGBW" :
                            (o.protocol == LED_WS2811) ? "WS2811" : "WS2812B";

    char n[12];
    char iS[4]; snprintf(iS, sizeof(iS), "%d", i);

    // ── Card ───────────────────────────────────────────────────────────────
    out += "<div class=\"ch-card\" id=\"card-"; out +=iS; out +="\">";

    // Head (click toggles card open)
    out +="<div class=\"ch-head\" onclick=\"toggleCh("; out +=iS; out +=")\">";
    out +="<div class=\"ch-id\">";
    out +="<span class=\"ch-num\">CH"; snprintf(n, sizeof(n), "%d", i+1); out +=n; out +="</span>";
    out +="<span class=\"ch-gpio\">GPIO "; snprintf(n, sizeof(n), "%d", gpio); out +=n; out +="</span>";
    out +="</div>";
    out +="<div class=\"ch-sum\">";
    out +="<span class=\"ch-proto\" id=\"sProto"; out +=iS; out +="\">"; out +=protoName; out +="</span>";
    out +="<div class=\"ch-tags\">";
    out +="<span class=\"ch-tag\" id=\"sP1"; out +=iS; out +="\">";
    if (isPwm)        { snprintf(n, sizeof(n), "%u", (unsigned)o.pwm_freq_hz);   out +="<b>"; out +=n; out +="Hz</b>"; }
    else if (isRelay) { snprintf(n, sizeof(n), "%u", (unsigned)o.relay_threshold); out +="thr:<b>"; out +=n; out +="</b>"; }
    else              { snprintf(n, sizeof(n), "%u", (unsigned)o.pixel_count);    out +="<b>"; out +=n; out +="</b> px"; }
    out +="</span>";
    out +="<span class=\"ch-tag\">U:<b id=\"sU"; out +=iS; out +="\">";
    snprintf(n, sizeof(n), "%u", (unsigned)o.universe_start); out +=n; out +="</b></span>";
    out +="<span class=\"ch-tag\">CH:<b id=\"sCH"; out +=iS; out +="\">";
    snprintf(n, sizeof(n), "%u", (unsigned)o.dmx_start); out +=n; out +="</b></span>";
    out +="</div></div>";
    out +="<span class=\"ch-arr\">&#9661;</span>";
    out +="</div>";

    // Body
    out +="<div class=\"ch-body\">";
    out +="<div class=\"ch-form\">";

    // Protocol select
    out +="<div class=\"field\"><label class=\"lbl\">Protocol</label>";
    out +="<select name=\"elyonProto"; out +=iS; out +="\" id=\"proto"; out +=iS;
    out +="\" onchange=\"protoChange("; out +=iS; out +=")\">";
    out +="<option value=\"0\"";  if (o.protocol == LED_WS2811)  out +=" selected"; out +=">WS2811</option>";
    out +="<option value=\"1\"";  if (o.protocol == LED_WS2812B) out +=" selected"; out +=">WS2812B</option>";
    out +="<option value=\"2\"";  if (o.protocol == LED_SK6812)  out +=" selected"; out +=">SK6812 RGBW</option>";
    out +="<option value=\"3\"";  if (o.protocol == LED_WS2814)  out +=" selected"; out +=">WS2814 RGBW</option>";
    out +="<option value=\"50\""; if (isPwm)   out +=" selected"; out +=">PWM Dimmer</option>";
    out +="<option value=\"51\""; if (isRelay) out +=" selected"; out +=">Relay</option>";
    out +="</select></div>";

    // Relay section
    out +="<div id=\"relaySec"; out +=iS; out +="\"";
    if (!isRelay) out +=" style=\"display:none\"";
    out +=">";
    out +="<div class=\"field\"><label class=\"lbl\">ON Threshold (0–255)</label>";
    out +="<input type=\"number\" name=\"elyonRelayThr"; out +=iS; out +="\" id=\"rthr_"; out +=iS;
    out +="\" value=\""; snprintf(n, sizeof(n), "%u", (unsigned)o.relay_threshold); out +=n;
    out +="\" min=\"0\" max=\"255\"></div>";
    out +="<div class=\"field-note\" style=\"padding:0 2px 4px\">OFF when DMX &lt; threshold &middot; ON when DMX &ge; threshold</div>";
    out +="</div>";

    // PWM section
    out +="<div id=\"pwmSec"; out +=iS; out +="\"";
    if (!isPwm) out +=" style=\"display:none\"";
    out +=">";
    out +="<div class=\"field\"><label class=\"lbl\">PWM Frequency</label>";
    out +="<select name=\"elyonPwmFreq"; out +=iS; out +="\" id=\"freq"; out +=iS; out +="\" onchange=\"elyonRecalc()\">";
    out +="<option value=\"100\"";   if (o.pwm_freq_hz == 100)                              out +=" selected"; out +=">100 Hz</option>";
    out +="<option value=\"500\"";   if (o.pwm_freq_hz == 500)                              out +=" selected"; out +=">500 Hz</option>";
    out +="<option value=\"1000\"";  if (o.pwm_freq_hz == 1000 || (isPwm && !o.pwm_freq_hz)) out +=" selected"; out +=">1 kHz</option>";
    out +="<option value=\"5000\"";  if (o.pwm_freq_hz == 5000)                             out +=" selected"; out +=">5 kHz</option>";
    out +="<option value=\"10000\""; if (o.pwm_freq_hz == 10000)                            out +=" selected"; out +=">10 kHz</option>";
    out +="<option value=\"20000\""; if (o.pwm_freq_hz == 20000)                            out +=" selected"; out +=">20 kHz</option>";
    out +="</select></div>";
    out +="<div class=\"g2\" style=\"margin-top:8px\">";
    out +="<div class=\"field\"><label class=\"lbl\">Curve</label>";
    out +="<select name=\"elyonCurve"; out +=iS; out +="\" id=\"curve_"; out +=iS; out +="\">";
    out +="<option value=\"0\""; if (o.pwm_curve == 0) out +=" selected"; out +=">Linear</option>";
    out +="<option value=\"1\""; if (o.pwm_curve == 1) out +=" selected"; out +=">Quad \xce\xb3" "2</option>";
    out +="<option value=\"2\""; if (o.pwm_curve == 2) out +=" selected"; out +=">Cubic \xce\xb3" "3</option>";
    out +="</select></div>";
    out +="<div style=\"display:flex;flex-direction:column;justify-content:flex-end;padding-bottom:10px;gap:8px\">";
    out +="<div class=\"tog-row\">";
    out +="<input type=\"checkbox\" name=\"elyonPwm16"; out +=iS; out +="\" id=\"bit16_"; out +=iS; out +="\"";
    if (isPwm && o.pwm_16bit) out +=" checked";
    out +=" onchange=\"elyonRecalc()\">";
    out +="<span class=\"tog-lbl\">16-bit</span></div>";
    out +="<div class=\"tog-row\">";
    out +="<input type=\"checkbox\" name=\"elyonPwmInv"; out +=iS; out +="\" id=\"pwminv_"; out +=iS; out +="\"";
    if (isPwm && o.pwm_invert) out +=" checked";
    out +=">";
    out +="<span class=\"tog-lbl\">Invert</span></div></div>";
    out +="</div>";
    out +="</div>";

    // Pixel section
    out +="<div id=\"pxSec"; out +=iS; out +="\"";
    if (!isPx) out +=" style=\"display:none\"";
    out +=">";
    out +="<div class=\"g2\">";
    out +="<div class=\"field\"><label class=\"lbl\">Pixel count</label>";
    out +="<input type=\"number\" name=\"elyonCount"; out +=iS; out +="\" id=\"count_"; out +=iS;
    out +="\" value=\""; snprintf(n, sizeof(n), "%u", (unsigned)o.pixel_count); out +=n;
    out +="\" min=\"0\" max=\"4096\" onchange=\"elyonRecalc()\"></div>";
    out +="<div class=\"field\"><label class=\"lbl\">Color order</label>";
    out +="<input type=\"text\" name=\"elyonOrder"; out +=iS; out +="\" id=\"order_"; out +=iS;
    out +="\" value=\""; out +=order_str;
    out +="\" maxlength=\"4\" oninput=\"this.value=this.value.toUpperCase()\" style=\"text-align:center;letter-spacing:.1em\"></div>";
    out +="</div>";
    out +="<div class=\"tog-row\" style=\"margin-top:8px\">";
    out +="<input type=\"checkbox\" name=\"elyonInv"; out +=iS; out +="\" id=\"inv_"; out +=iS; out +="\"";
    if (isPx && o.invert) out +=" checked";
    out +="><span class=\"tog-lbl\">Invert signal</span></div>";
    out +="<div class=\"field\" style=\"margin-top:8px\"><label class=\"lbl\">Group</label>";
    out +="<input type=\"number\" name=\"elyonGroup"; out +=iS; out +="\" id=\"grp_"; out +=iS;
    out +="\" value=\""; snprintf(n, sizeof(n), "%u", (unsigned)o.grouping); out +=n;
    out +="\" min=\"1\" max=\"255\" onchange=\"elyonRecalc()\"></div>";
    out +="</div>";

    out +="<div class=\"div\"></div>";

    // Brightness (always visible)
    out +="<div class=\"field\"><label class=\"lbl\">Brightness</label>";
    out +="<input type=\"number\" name=\"elyonBri"; out +=iS; out +="\" id=\"bri_"; out +=iS;
    out +="\" value=\""; snprintf(n, sizeof(n), "%u", (unsigned)o.brightness); out +=n;
    out +="\" min=\"0\" max=\"255\"></div>";

    out +="<div class=\"div\"></div>";

    // Universe + Start CH
    out +="<div class=\"g2\">";
    out +="<div class=\"field\"><label class=\"lbl\">Universe</label>";
    out +="<input type=\"number\" name=\"elyonUniv"; out +=iS; out +="\" id=\"univ_"; out +=iS;
    out +="\" value=\""; snprintf(n, sizeof(n), "%u", (unsigned)o.universe_start); out +=n;
    out +="\" min=\"0\" max=\"32767\" onchange=\"sumUpdate("; out +=iS; out +=")\"></div>";
    out +="<div class=\"field\"><label class=\"lbl\">Start CH</label>";
    out +="<input type=\"number\" name=\"elyonCh"; out +=iS; out +="\" id=\"sch_"; out +=iS;
    out +="\" value=\""; snprintf(n, sizeof(n), "%u", (unsigned)o.dmx_start); out +=n;
    out +="\" min=\"1\" max=\"512\" onchange=\"sumUpdate("; out +=iS; out +=")\"></div>";
    out +="</div>";

    out +="</div></div></div>";
}

void injectFixturePlaceholders(String& html) {
    // (dead code — streaming path uses writeFixtureVars; kept for completeness)
    static const char tpl[] = ELYON_FIXTURE_HTML;
    static const char rowsPH[] = "{{ELYON_ROWS}}";
    const char* split = strstr(tpl, rowsPH);
    String section;
    if (split) {
        size_t beforeLen = (size_t)(split - tpl);
        size_t afterLen  = strlen(split + sizeof(rowsPH) - 1);
        section.reserve(beforeLen + (size_t)HW_LED_OUTPUT_COUNT * 2400 + afterLen);
        section.concat(tpl, beforeLen);
        for (int i = 0; i < ELYON_NUM_OUTPUTS; i++) appendElyonCard(section, i);
        section.concat(split + sizeof(rowsPH) - 1, afterLen);
    } else {
        section = tpl;
    }
    html.replace("{{FIXTURE_SECTION}}", section);
    section = String();
    html.replace("{{FIXTURE_JS}}",           ELYON_FIXTURE_JS);
    html.replace("{{fixture_display_name}}", ELYON_FIXTURE_NAME);
}

void handleFixtureSaveParams(AsyncWebServerRequest* request, bool& needsRestart) {
    bool changed = false;

    // Budget check: skip PWM outputs (they have no pixel count)
    uint32_t totalPixels = 0;
    for (int i = 0; i < HW_LED_OUTPUT_COUNT; i++) {
        String protoKey = "elyonProto" + String(i);
        uint8_t proto = request->hasParam(protoKey, true)
                        ? (uint8_t)request->getParam(protoKey, true)->value().toInt()
                        : (uint8_t)elyonConfig.outputs[i].protocol;
        if (proto == (uint8_t)LED_PWM || proto == (uint8_t)LED_RELAY) continue;
        String countKey = "elyonCount" + String(i);
        uint32_t count = request->hasParam(countKey, true)
                         ? (uint32_t)request->getParam(countKey, true)->value().toInt()
                         : (uint32_t)elyonConfig.outputs[i].pixel_count;
        if (count > ELYON_MAX_PIXELS_PER_OUT) return;
        totalPixels += count;
    }
    if (totalPixels > ELYON_MAX_PIXELS_TOTAL) return;

    for (int i = 0; i < HW_LED_OUTPUT_COUNT; i++) {
        elyon_output_cfg_t& o = elyonConfig.outputs[i];

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

        if (newProto == LED_RELAY) {
            uint8_t newThr = getU8("elyonRelayThr", o.relay_threshold);
            if (newProto != o.protocol || newThr != o.relay_threshold ||
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
        } else {
            uint16_t newCount = getU16("elyonCount", o.pixel_count);
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

            if (newProto != o.protocol || newCount != o.pixel_count ||
                newUniv != o.universe_start || newCh != o.dmx_start ||
                newGroup != o.grouping || newInv != o.invert || newBri != o.brightness || orderChanged) {
                o.protocol       = newProto;
                o.pixel_count    = newCount;
                o.universe_start = newUniv;
                o.dmx_start      = newCh;
                o.grouping       = newGroup;
                o.invert         = newInv;
                o.brightness     = newBri;
                memcpy(o.color_order, newOrder, 4);
                o.pwm_freq_hz    = 0;
                o.pwm_curve      = 0;
                o.pwm_16bit      = 0;
                changed = true;
            }
        }
    }

    if (changed) {
        saveConfig();
        needsRestart = true;
    }
}

void registerFixtureRoutes(AsyncWebServer& server) {
    (void)server;
}

#endif // RAVLIGHT_FIXTURE_ELYON
