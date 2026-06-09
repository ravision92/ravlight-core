#ifdef RAVLIGHT_FIXTURE_ELYON
#include "fixture_webserver.h"
#include "fixtures/elyon/fixture_html.h"
#include "fixtures/elyon/fixture.h"
#include "config.h"
#include "dmx_manager.h"
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
    } else if (strcmp(var, "fixture_tab1_name") == 0) {
        out.concat(ELYON_FIXTURE_NAME);   // single fixture tab
    } else if (strcmp(var, "dmx_universe_note") == 0) {
        out.concat("Base universe; each LED output can be set to its own universe above.");
    } else if (strcmp(var, "ELYON_ROWS") == 0) {
        for (int i = 0; i < HW_LED_OUTPUT_COUNT; i++) appendElyonCard(out, i);
    }
}

// Appends a card-based output block directly into `out` (no intermediate String allocation).
// Uses ch-card/ch-head/ch-body CSS from index.html; server-renders current config values.
static void appendElyonCard(String& out, int i) {
    const led_output_cfg_t& o = elyonConfig.outputs[i];
    int gpio = (i < HW_LED_OUTPUT_COUNT) ? HW_LED_OUTPUT_PINS[i] : -1;
    bool isPwm    = (o.protocol == LED_PWM);
    bool isRelay  = (o.protocol == LED_RELAY);
    bool isFollow = (o.protocol == LED_CLOCK_FOLLOWER);
    bool isClk    = led_is_clocked(o.protocol);
    // Pixel-family layout (px/clocked share count/order/group/bri/invert fields)
    bool isPx     = !isPwm && !isRelay && !isFollow;

    // For a FOLLOWER output, find which other output claims us as CLOCK partner.
    int followerOwner = -1;
    if (isFollow) {
        for (int j = 0; j < ELYON_NUM_OUTPUTS; j++) {
            if (j == i) continue;
            const led_output_cfg_t& oj = elyonConfig.outputs[j];
            if (led_is_clocked(oj.protocol) && oj.clock_partner_idx == i) {
                followerOwner = j; break;
            }
        }
    }

    char order_str[5];
    color_order_to_str(o.color_order,
                       isPx ? led_ch_per_pixel(o.protocol) : 3, order_str);

    const char* protoName =
        isPwm                          ? "PWM Dimmer" :
        isRelay                        ? "Relay" :
        isFollow                       ? "CLOCK follower" :
        (o.protocol == LED_SK6812)     ? "SK6812 RGBW" :
        (o.protocol == LED_WS2814)     ? "WS2814 RGBW" :
        (o.protocol == LED_WS2815)     ? "WS2815" :
        (o.protocol == LED_TM1814)     ? "TM1814 RGBW" :
        (o.protocol == LED_TM1914)     ? "TM1914 RGBW" :
        (o.protocol == LED_APA102)     ? "APA102" :
        (o.protocol == LED_SK9822)     ? "SK9822" :
        (o.protocol == LED_P9813)      ? "P9813" :
        (o.protocol == LED_WS2811)     ? "WS2811" : "WS2812B";

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

    // Body — followers render a minimal "consumed" notice instead of the full form.
    out +="<div class=\"ch-body\">";
    out +="<div class=\"ch-form\">";

    if (isFollow) {
        out +="<p class=\"field-note\">This output is used as the <b>CLOCK</b> line of ";
        if (followerOwner >= 0) {
            char on[12]; snprintf(on, sizeof(on), "%d", followerOwner + 1);
            out +="<b>CH"; out +=on; out +="</b>";
        } else {
            out +="<b>another output</b>";
        }
        out +=". Configure or free it from there.</p>";
        // Hidden inputs so the save handler still receives this output's identity
        // (avoid orphaning the slot on /save). The protocol is forwarded so the
        // FOLLOWER state persists round-trip.
        out +="<input type=\"hidden\" name=\"elyonProto"; out +=iS; out +="\" value=\"60\">";
        out +="</div></div></div>";
        return;
    }

    // Protocol select
    out +="<div class=\"field\"><label class=\"lbl\">Protocol</label>";
    out +="<select name=\"elyonProto"; out +=iS; out +="\" id=\"proto"; out +=iS;
    out +="\" onchange=\"protoChange("; out +=iS; out +=")\">";
    out +="<option value=\"0\"";  if (o.protocol == LED_WS2811)  out +=" selected"; out +=">WS2811</option>";
    out +="<option value=\"1\"";  if (o.protocol == LED_WS2812B) out +=" selected"; out +=">WS2812B</option>";
    out +="<option value=\"2\"";  if (o.protocol == LED_SK6812)  out +=" selected"; out +=">SK6812 RGBW</option>";
    out +="<option value=\"3\"";  if (o.protocol == LED_WS2814)  out +=" selected"; out +=">WS2814 RGBW</option>";
    out +="<option value=\"4\"";  if (o.protocol == LED_WS2815)  out +=" selected"; out +=">WS2815</option>";
    out +="<option value=\"5\"";  if (o.protocol == LED_TM1814)  out +=" selected"; out +=">TM1814 RGBW</option>";
    out +="<option value=\"6\"";  if (o.protocol == LED_TM1914)  out +=" selected"; out +=">TM1914 RGBW</option>";
    out +="<option value=\"7\"";  if (o.protocol == LED_APA102)  out +=" selected"; out +=">APA102 (clocked)</option>";
    out +="<option value=\"8\"";  if (o.protocol == LED_SK9822)  out +=" selected"; out +=">SK9822 (clocked)</option>";
    out +="<option value=\"9\"";  if (o.protocol == LED_P9813)   out +=" selected"; out +=">P9813 (clocked)</option>";
    out +="<option value=\"50\""; if (isPwm)   out +=" selected"; out +=">PWM Dimmer</option>";
    out +="<option value=\"51\""; if (isRelay) out +=" selected"; out +=">Relay</option>";
    out +="</select></div>";

    // Backend selector — only shown on builds that ship the I2S driver (the whole
    // field is removed by the F.i2s feature-flag filter on RMT-only firmware).
    // Hidden by JS for PWM/Relay/Clocked (no backend choice — they use their own
    // peripherals: LEDC, GPIO, bit-bang).
    out +="<div class=\"field\" data-module=\"i2s\" id=\"backendSec"; out +=iS; out +="\"";
    if (isPwm || isRelay || isClk || isFollow) out +=" style=\"display:none\"";
    out +="><label class=\"lbl\">Driver backend</label>";
    out +="<select name=\"elyonBackend"; out +=iS; out +="\" id=\"backend_"; out +=iS; out +="\" data-restart=\"1\" onchange=\"elyonRecalc()\">";
    out +="<option value=\"0\""; if (o.backend == (uint8_t)LED_BACKEND_RMT) out +=" selected"; out +=">RMT (1 channel per output, max 8)</option>";
    out +="<option value=\"1\""; if (o.backend == (uint8_t)LED_BACKEND_I2S) out +=" selected"; out +=">I2S (shared parallel, max 8)</option>";
    out +="</select>";
    out +="<div class=\"field-note\" style=\"padding:0 2px 4px\">Changing the backend requires a device restart.</div>";
    out +="</div>";

    // Relay section
    out +="<div id=\"relaySec"; out +=iS; out +="\"";
    if (!isRelay) out +=" style=\"display:none\"";
    out +=">";
    out +="<div class=\"field\"><label class=\"lbl\">ON Threshold (0–255)</label>";
    out +="<input type=\"number\" name=\"elyonRelayThr"; out +=iS; out +="\" id=\"rthr_"; out +=iS;
    out +="\" value=\""; snprintf(n, sizeof(n), "%u", (unsigned)o.relay_threshold); out +=n;
    out +="\" min=\"0\" max=\"255\"></div>";
    out +="<div class=\"field-note\" style=\"padding:0 2px 4px\">OFF when DMX &lt; threshold &middot; ON when DMX &ge; threshold</div>";
    out +="<div class=\"tog-row\" style=\"margin-top:6px\">";
    out +="<input type=\"checkbox\" name=\"elyonRelayInv"; out +=iS; out +="\" id=\"rinv_"; out +=iS; out +="\"";
    if (isRelay && o.relay_invert) out +=" checked";
    out +="><span class=\"tog-lbl\">Active-low (invert output)</span></div>";
    out +="</div>";

    // Clocked section — pick which other output's pin is used as the CLOCK line.
    // The chosen partner gets its protocol forced to LED_CLOCK_FOLLOWER on save.
    out +="<div id=\"clockSec"; out +=iS; out +="\"";
    if (!isClk) out +=" style=\"display:none\"";
    out +=">";
    out +="<div class=\"field\"><label class=\"lbl\">CLOCK partner output</label>";
    out +="<select name=\"elyonClockPartner"; out +=iS; out +="\" id=\"clkp_"; out +=iS; out +="\">";
    for (int j = 0; j < HW_LED_OUTPUT_COUNT; j++) {
        if (j == i) continue;
        out +="<option value=\""; snprintf(n, sizeof(n), "%d", j); out +=n; out +="\"";
        if (isClk && o.clock_partner_idx == j) out +=" selected";
        out +=">CH"; snprintf(n, sizeof(n), "%d", j + 1); out +=n;
        out +=" (GPIO "; snprintf(n, sizeof(n), "%d", HW_LED_OUTPUT_PINS[j]); out +=n; out +=")</option>";
    }
    out +="</select></div>";
    out +="<div class=\"field-note\" style=\"padding:0 2px 4px\">The chosen output will be reserved as CLOCK line and cannot be configured independently.</div>";
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

    // Brightness — hidden for relay (irrelevant), shown for pixel and PWM
    out +="<div class=\"field\" id=\"briSec"; out +=iS; out +="\"";
    if (isRelay) out +=" style=\"display:none\"";
    out +="><label class=\"lbl\">Brightness</label>";
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

    // Highlight (white wipe) — identification
    out +="<button type=\"button\" class=\"act-btn\" style=\"margin-top:10px;border-radius:var(--r);padding:9px;font-size:12px\" onclick=\"elyonHighlight("; out +=iS; out +=")\">Highlight (white wipe)</button>";

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
        // Per-card budget: WS-family options + clocked options + clockSec partner select (~N options ×
        // ~70 B) + PWM/Relay/highlight/buttons. Bumped from 2400 to 3500 after Phase 5 (clocked UI).
        section.reserve(beforeLen + (size_t)HW_LED_OUTPUT_COUNT * 3500 + afterLen);
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
