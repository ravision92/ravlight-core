#ifdef RAVLIGHT_FIXTURE_ELYON
#include "fixture_config.h"
#include "fixtures/elyon/fixture.h"

ElyonConfig elyonConfig;

void fixtureConfigDefaults() {
    for (int i = 0; i < ELYON_NUM_OUTPUTS; i++) {
        elyon_output_cfg_t& o = elyonConfig.outputs[i];
        o.protocol       = LED_WS2812B;
        o.pixel_count    = (i == 0) ? 30 : 0;
        o.universe_start = (uint16_t)i;
        o.dmx_start      = 1;
        o.grouping       = 1;
        o.invert         = 0;
        o.brightness     = 255;
        o.color_order[0] = 0;  // R
        o.color_order[1] = 1;  // G
        o.color_order[2] = 2;  // B
        o.color_order[3] = 3;  // W (ignored for RGB protocols)
    }
}

void fixtureConfigSerialize(JsonObject& fix) {
    JsonArray outputs = fix.createNestedArray("outputs");
    for (int i = 0; i < ELYON_NUM_OUTPUTS; i++) {
        const elyon_output_cfg_t& o = elyonConfig.outputs[i];
        JsonObject out = outputs.createNestedObject();
        out["proto"] = (int)o.protocol;
        out["count"] = o.pixel_count;
        out["univ"]  = o.universe_start;
        out["ch"]    = o.dmx_start;
        out["group"] = o.grouping;
        out["inv"]   = o.invert;
        out["bri"]   = o.brightness;
        char order_str[5];
        color_order_to_str(o.color_order, led_ch_per_pixel(o.protocol), order_str);
        out["order"] = order_str;
    }
}

void fixtureConfigDeserialize(const JsonObject& fix) {
    JsonArrayConst outputs = fix["outputs"].as<JsonArrayConst>();
    for (int i = 0; i < ELYON_NUM_OUTPUTS; i++) {
        elyon_output_cfg_t& o = elyonConfig.outputs[i];
        JsonObjectConst out   = outputs[i].as<JsonObjectConst>();
        o.protocol       = (led_protocol_t)(out["proto"] | (int)LED_WS2812B);
        o.pixel_count    = out["count"] | (uint16_t)(i == 0 ? 30 : 0);
        o.universe_start = out["univ"]  | (uint16_t)i;
        o.dmx_start      = out["ch"]    | (uint16_t)1;
        o.grouping       = out["group"] | (uint8_t)1;
        o.invert         = out["inv"]   | (uint8_t)0;
        o.brightness     = out["bri"]   | (uint8_t)255;
        if (o.grouping == 0) o.grouping = 1;
        // color_order: default RGB(W) if key missing (backward compat)
        const char* order_str = out["order"] | "";
        if (order_str[0]) {
            color_order_from_str(order_str, o.color_order);
        } else {
            o.color_order[0] = 0; o.color_order[1] = 1;
            o.color_order[2] = 2; o.color_order[3] = 3;
        }
    }
}

#endif // RAVLIGHT_FIXTURE_ELYON
