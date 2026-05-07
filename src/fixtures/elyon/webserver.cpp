#ifdef RAVLIGHT_FIXTURE_ELYON
#include "fixture_webserver.h"
#include "fixtures/elyon/fixture_html.h"
#include "fixtures/elyon/fixture.h"
#include "config.h"
#include <string.h>

static String buildElyonRow(int i);  // forward declaration — defined below

void writeFixtureVars(String& out, const char* var) {
    if (strcmp(var, "FIXTURE_SECTION") == 0) {
        // ELYON_FIXTURE_HTML has exactly one inner placeholder: {{ELYON_ROWS}}
        static const char tpl[] = ELYON_FIXTURE_HTML;
        static const char ph[]  = "{{ELYON_ROWS}}";
        const char* split = strstr(tpl, ph);
        if (split) {
            out.concat((const char*)tpl, split - tpl);
            for (int i = 0; i < HW_LED_OUTPUT_COUNT; i++) {
                String row = buildElyonRow(i);
                out.concat(row);
                row = String();  // free immediately after concat
            }
            out.concat(split + sizeof(ph) - 1);
        } else {
            out.concat(tpl);
        }
    } else if (strcmp(var, "FIXTURE_JS") == 0) {
        out.concat(ELYON_FIXTURE_JS);
    } else if (strcmp(var, "fixture_display_name") == 0) {
        out.concat(ELYON_FIXTURE_NAME);
    } else if (strcmp(var, "ELYON_ROWS") == 0) {
        for (int i = 0; i < HW_LED_OUTPUT_COUNT; i++) {
            String row = buildElyonRow(i);
            out.concat(row);
            row = String();
        }
    }
}

// Two <tr> per output.
// Row 1: CH+Inv (rowspan=2) | Type select | Pixels | Group
// Row 2: (spanned)          | Order + Bri | Universe | CH start
//
// All number/string conversions use snprintf into a small stack buffer to avoid
// temporary String heap allocations — prevents heap fragmentation crash on boards
// with many large outputs (e.g. 6× 200px) where free heap is tight during page load.
static String buildElyonRow(int i) {
    const elyon_output_cfg_t& o = elyonConfig.outputs[i];
    int gpio = (i < HW_LED_OUTPUT_COUNT) ? HW_LED_OUTPUT_PINS[i] : -1;

    char order_str[5];
    color_order_to_str(o.color_order, led_ch_per_pixel(o.protocol), order_str);

    const char* tdP  = "padding:6px 6px;vertical-align:middle;";
    const char* tdP2 = "padding:6px 6px 16px;vertical-align:middle;";
    const char* inp  = "padding:5px 4px;margin:0;font-size:1em;";
    const char* lbl  = "display:block;font-size:0.75em;color:#666;margin-bottom:3px;";

    // Single stack buffer for all integer→string conversions — no heap String temps
    char n[12];
    // Index strings reused across the function
    char iS[4]; snprintf(iS, sizeof(iS), "%d", i);

    String row;
    row.reserve(1600);

    // ── Row 1 ──────────────────────────────────────────────────────────────
    row = "<tr>";

    // CH + Inv — rowspan=2
    row += "<td rowspan=\"2\" style=\"padding:6px 10px 6px 0;vertical-align:middle;white-space:nowrap;\">";
    row += "<span style=\"font-weight:bold;color:#e9ff00;\">CH";
    snprintf(n, sizeof(n), "%d", i+1); row += n;
    row += "</span>";
    row += "<span style=\"display:block;color:#444;font-size:0.8em;margin:2px 0 6px;\">GPIO ";
    snprintf(n, sizeof(n), "%d", gpio); row += n;
    row += "</span>";
    row += "<label style=\"display:flex;align-items:center;gap:4px;margin:0;width:auto;"
           "font-size:0.9em;color:#888;cursor:pointer;\">"
           "<input type=\"checkbox\" name=\"elyonInv"; row += iS; row += "\"";
    if (o.invert) row += " checked";
    row += " style=\"width:16px;height:16px;margin:0;flex-shrink:0;\"> Inv</label>";
    row += "</td>";

    // Type
    row += "<td style=\""; row += tdP; row += "\">";
    row += "<select name=\"elyonProto"; row += iS;
    row += "\" style=\"width:auto;"; row += inp; row += "\" onchange=\"elyonRecalc()\">";
    row += "<option value=\"0\""; if (o.protocol == LED_WS2811)  row += " selected"; row += ">WS2811</option>";
    row += "<option value=\"1\""; if (o.protocol == LED_WS2812B) row += " selected"; row += ">WS2812B</option>";
    row += "<option value=\"2\""; if (o.protocol == LED_SK6812)  row += " selected"; row += ">SK6812 RGBW</option>";
    row += "<option value=\"3\""; if (o.protocol == LED_WS2814)  row += " selected"; row += ">WS2814 RGBW</option>";
    row += "</select></td>";

    // Pixels
    row += "<td style=\""; row += tdP; row += "\">";
    row += "<input type=\"number\" name=\"elyonCount"; row += iS;
    row += "\" value=\""; snprintf(n, sizeof(n), "%u", (unsigned)o.pixel_count); row += n;
    row += "\" min=\"0\" max=\"4096\" style=\"width:62px;"; row += inp;
    row += "\" onchange=\"elyonRecalc()\"></td>";

    // Group
    row += "<td style=\""; row += tdP; row += "\">";
    row += "<input type=\"number\" name=\"elyonGroup"; row += iS;
    row += "\" value=\""; snprintf(n, sizeof(n), "%u", (unsigned)o.grouping); row += n;
    row += "\" min=\"1\" max=\"255\" style=\"width:52px;"; row += inp;
    row += "\" onchange=\"elyonRecalc()\"></td>";
    row += "</tr>";

    // ── Row 2 ──────────────────────────────────────────────────────────────
    row += "<tr style=\"border-bottom:1px solid #1e1e1e;\">";

    // Order + Bri
    row += "<td style=\""; row += tdP2; row += "\">";
    row += "<div style=\"display:flex;gap:10px;align-items:flex-end;\">";
    row += "<div><span style=\""; row += lbl; row += "\">Order</span>";
    row += "<input type=\"text\" name=\"elyonOrder"; row += iS;
    row += "\" value=\""; row += order_str;
    row += "\" maxlength=\"4\" style=\"width:50px;"; row += inp;
    row += "text-transform:uppercase;\" oninput=\"this.value=this.value.toUpperCase()\"></div>";
    row += "<div><span style=\""; row += lbl; row += "\">Bri</span>";
    row += "<input type=\"number\" name=\"elyonBri"; row += iS;
    row += "\" value=\""; snprintf(n, sizeof(n), "%u", (unsigned)o.brightness); row += n;
    row += "\" min=\"0\" max=\"255\" style=\"width:50px;"; row += inp; row += "\"></div></div></td>";

    // Universe
    row += "<td style=\""; row += tdP2; row += "\">";
    row += "<span style=\""; row += lbl; row += "\">Universe</span>";
    row += "<input type=\"number\" name=\"elyonUniv"; row += iS;
    row += "\" id=\"elyonUniv"; row += iS;
    row += "\" value=\""; snprintf(n, sizeof(n), "%u", (unsigned)o.universe_start); row += n;
    row += "\" min=\"0\" max=\"32767\" style=\"width:62px;"; row += inp; row += "\"></td>";

    // CH start
    row += "<td style=\""; row += tdP2; row += "\">";
    row += "<span style=\""; row += lbl; row += "\">CH</span>";
    row += "<input type=\"number\" name=\"elyonCh"; row += iS;
    row += "\" id=\"elyonCh"; row += iS;
    row += "\" value=\""; snprintf(n, sizeof(n), "%u", (unsigned)o.dmx_start); row += n;
    row += "\" min=\"1\" max=\"512\" style=\"width:52px;"; row += inp; row += "\"></td>";
    row += "</tr>";

    return row;
}

void injectFixturePlaceholders(String& html) {
    // Build rows without pre-reserving a large block — each buildElyonRow() call
    // uses only a single 1600-byte heap allocation (reserved upfront in the function),
    // so peak per-call heap is 1600 bytes instead of 1600 + N temp Strings.
    String rows;
    for (int i = 0; i < ELYON_NUM_OUTPUTS; i++) rows += buildElyonRow(i);

    // Split ELYON_FIXTURE_HTML at {{ELYON_ROWS}} and build section without an
    // intermediate copy: concat before-fragment + rows + after-fragment in one pass.
    // Then free rows immediately so section + html don't coexist with rows.
    static const char tpl[] = ELYON_FIXTURE_HTML;
    static const char rowsPH[] = "{{ELYON_ROWS}}";
    const char* split = strstr(tpl, rowsPH);
    String section;
    if (split) {
        size_t beforeLen = (size_t)(split - tpl);
        size_t afterLen  = strlen(split + sizeof(rowsPH) - 1);
        section.reserve(beforeLen + rows.length() + afterLen);
        section.concat(tpl, beforeLen);
        section += rows;
        rows = String();  // free rows now — section doesn't need it anymore
        section.concat(split + sizeof(rowsPH) - 1, afterLen);
    } else {
        section = tpl;
        rows = String();
    }

    html.replace("{{FIXTURE_SECTION}}", section);
    section = String();  // free section before remaining replacements
    html.replace("{{FIXTURE_JS}}",           ELYON_FIXTURE_JS);
    html.replace("{{fixture_display_name}}", ELYON_FIXTURE_NAME);
}

void handleFixtureSaveParams(AsyncWebServerRequest* request, bool& needsRestart) {
    bool changed = false;

    uint32_t totalPixels = 0;
    for (int i = 0; i < HW_LED_OUTPUT_COUNT; i++) {
        String key = "elyonCount" + String(i);
        uint32_t ch = request->hasParam(key, true)
                      ? (uint32_t)request->getParam(key, true)->value().toInt()
                      : elyonConfig.outputs[i].pixel_count;
        if (ch > ELYON_MAX_PIXELS_PER_OUT) return;
        totalPixels += ch;
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

        led_protocol_t newProto = (led_protocol_t)getU8 ("elyonProto", (uint8_t)o.protocol);
        uint16_t       newCount = getU16("elyonCount", o.pixel_count);
        uint16_t       newUniv  = getU16("elyonUniv",  o.universe_start);
        uint16_t       newCh    = getU16("elyonCh",    o.dmx_start);
        uint8_t        newGroup = getU8 ("elyonGroup",  o.grouping);
        uint8_t        newInv   = request->hasParam("elyonInv" + String(i), true) ? 1 : 0;
        uint8_t        newBri   = getU8 ("elyonBri",    o.brightness);
        if (newGroup == 0) newGroup = 1;
        if (newCh    == 0) newCh    = 1;

        uint8_t newOrder[4] = {o.color_order[0], o.color_order[1],
                               o.color_order[2], o.color_order[3]};
        String orderKey = "elyonOrder" + String(i);
        if (request->hasParam(orderKey, true)) {
            String v = request->getParam(orderKey, true)->value();
            v.toUpperCase();
            uint8_t ch_pp = led_ch_per_pixel(newProto);
            bool valid = ((int)v.length() == ch_pp);
            if (valid) color_order_from_str(v.c_str(), newOrder);
        }

        bool orderChanged = (newOrder[0] != o.color_order[0] || newOrder[1] != o.color_order[1] ||
                             newOrder[2] != o.color_order[2] || newOrder[3] != o.color_order[3]);

        if (newProto != o.protocol || newCount != o.pixel_count ||
            newUniv  != o.universe_start || newCh != o.dmx_start ||
            newGroup != o.grouping || newInv != o.invert || newBri != o.brightness || orderChanged) {
            o.protocol       = newProto;
            o.pixel_count    = newCount;
            o.universe_start = newUniv;
            o.dmx_start      = newCh;
            o.grouping       = newGroup;
            o.invert         = newInv;
            o.brightness     = newBri;
            memcpy(o.color_order, newOrder, 4);
            changed = true;
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
