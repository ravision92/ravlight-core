#ifdef RAVLIGHT_FIXTURE_ELYON
#include "fixtures/elyon/webserver.h"
#include "fixtures/elyon/fixture_html.h"
#include "fixtures/elyon/fixture.h"
#include "config.h"

// Build one <tr> row for output i from the current config.
static String buildElyonRow(int i) {
    const elyon_output_cfg_t& o = elyonConfig.outputs[i];
    int gpio = (i < HW_LED_OUTPUT_COUNT) ? HW_LED_OUTPUT_PINS[i] : -1;
    String row;
    row.reserve(600);
    row = "<tr>";
    row += "<td style=\"padding:3px 6px;\">CH" + String(i+1);
    row += " <span style=\"color:#666;\">(GPIO " + String(gpio) + ")</span></td>";

    // Protocol select
    row += "<td><select name=\"elyonProto" + String(i) + "\" onchange=\"elyonRecalc()\">";
    row += "<option value=\"0\"" + String(o.protocol == LED_WS2811  ? " selected" : "") + ">WS2811</option>";
    row += "<option value=\"1\"" + String(o.protocol == LED_WS2812B ? " selected" : "") + ">WS2812B</option>";
    row += "<option value=\"2\"" + String(o.protocol == LED_SK6812  ? " selected" : "") + ">SK6812 RGBW</option>";
    row += "<option value=\"3\"" + String(o.protocol == LED_WS2814  ? " selected" : "") + ">WS2814 RGBW</option>";
    row += "</select></td>";

    // Pixel count
    row += "<td><input type=\"number\" name=\"elyonCount" + String(i) + "\" value=\"" +
           String(o.pixel_count) + "\" min=\"0\" max=\"4096\" style=\"width:60px;\" onchange=\"elyonRecalc()\"></td>";

    // Universe (id for JS)
    row += "<td><input type=\"number\" name=\"elyonUniv" + String(i) +
           "\" id=\"elyonUniv" + String(i) + "\" value=\"" + String(o.universe_start) +
           "\" min=\"0\" max=\"32767\" style=\"width:60px;\"></td>";

    // DMX start channel (id for JS)
    row += "<td><input type=\"number\" name=\"elyonCh" + String(i) +
           "\" id=\"elyonCh" + String(i) + "\" value=\"" + String(o.dmx_start) +
           "\" min=\"1\" max=\"512\" style=\"width:55px;\"></td>";

    // Grouping
    row += "<td><input type=\"number\" name=\"elyonGroup" + String(i) + "\" value=\"" +
           String(o.grouping) + "\" min=\"1\" max=\"255\" style=\"width:45px;\" onchange=\"elyonRecalc()\"></td>";

    // Invert checkbox
    row += "<td style=\"text-align:center;\"><input type=\"checkbox\" name=\"elyonInv" + String(i) + "\"" +
           String(o.invert ? " checked" : "") + "></td>";

    // Brightness
    row += "<td><input type=\"number\" name=\"elyonBri" + String(i) + "\" value=\"" +
           String(o.brightness) + "\" min=\"0\" max=\"255\" style=\"width:50px;\"></td>";

    // Color order
    char order_str[5];
    color_order_to_str(o.color_order, led_ch_per_pixel(o.protocol), order_str);
    row += "<td><input type=\"text\" name=\"elyonOrder" + String(i) + "\" value=\"" +
           String(order_str) + "\" maxlength=\"4\" style=\"width:50px;text-transform:uppercase;\""
           " oninput=\"this.value=this.value.toUpperCase()\"></td>";

    row += "</tr>";
    return row;
}

void injectElyonPlaceholders(String& html) {
    String rows;
    rows.reserve(600 * ELYON_NUM_OUTPUTS);   // one allocation for all rows (~4.8 KB)
    for (int i = 0; i < ELYON_NUM_OUTPUTS; i++) rows += buildElyonRow(i);

    String section = ELYON_FIXTURE_HTML;
    section.replace("{{ELYON_ROWS}}", rows);

    html.replace("{{FIXTURE_SECTION}}",      section);
    html.replace("{{FIXTURE_JS}}",           ELYON_FIXTURE_JS);
    html.replace("{{fixture_display_name}}", ELYON_FIXTURE_NAME);
}

void handleElyonSaveParams(AsyncWebServerRequest* request, bool& needsRestart) {
    bool changed = false;

    uint32_t totalPixels = 0;
    for (int i = 0; i < ELYON_NUM_OUTPUTS; i++) {
        String key = "elyonCount" + String(i);
        uint32_t ch = request->hasParam(key, true)
                      ? (uint32_t)request->getParam(key, true)->value().toInt()
                      : elyonConfig.outputs[i].pixel_count;
        if (ch > ELYON_MAX_PIXELS_PER_OUT) return;   // per-output cap
        totalPixels += ch;
    }
    if (totalPixels > ELYON_MAX_PIXELS_TOTAL) return;

    for (int i = 0; i < ELYON_NUM_OUTPUTS; i++) {
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

        // Color order — parse new value; keep current if param missing or invalid
        uint8_t newOrder[4] = {o.color_order[0], o.color_order[1],
                               o.color_order[2], o.color_order[3]};
        String orderKey = "elyonOrder" + String(i);
        if (request->hasParam(orderKey, true)) {
            String v = request->getParam(orderKey, true)->value();
            v.toUpperCase();
            uint8_t ch_pp = led_ch_per_pixel(newProto);
            // Validate: must contain exactly R,G,B (and W for 4-ch) each once
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
        needsRestart = true;   // re-init strips + dmx_receiver required
    }
}

void registerElyonRoutes(AsyncWebServer& server) {
    (void)server;
}

#endif // RAVLIGHT_FIXTURE_ELYON
