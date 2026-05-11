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
        o.pwm_freq_hz    = 0;
        o.pwm_curve      = 0;
        o.pwm_16bit      = 0;
        o.pwm_invert     = 0;
        o.relay_threshold = 128;
    }

#ifdef BOARD_ELYON_PRESET_ALL_PWM
    // Apply board-specific preset: all outputs default to LED_PWM.
    // Runs on first boot (NVS empty) and factory reset.
    for (int i = 0; i < ELYON_NUM_OUTPUTS; i++) {
        elyon_output_cfg_t& o = elyonConfig.outputs[i];
        o.protocol       = LED_PWM;
        o.pwm_freq_hz    = BOARD_ELYON_PWM_DEFAULT_FREQ;
        o.pwm_curve      = 0;
        o.pwm_16bit      = 0;
        o.pwm_invert     = 0;
        o.pixel_count    = 0;
        o.grouping       = 1;
        o.invert         = 0;
        o.brightness     = 255;
        o.universe_start = (uint16_t)i;
        o.dmx_start      = 1;
        o.color_order[0] = 0; o.color_order[1] = 1;
        o.color_order[2] = 2; o.color_order[3] = 3;
        o.relay_threshold = 128;
    }
#ifdef BOARD_ELYON_RELAY_OUTPUT_IDX
    {
        elyon_output_cfg_t& r = elyonConfig.outputs[BOARD_ELYON_RELAY_OUTPUT_IDX];
        r.protocol        = LED_RELAY;
        r.relay_threshold = BOARD_ELYON_RELAY_THRESHOLD;
        r.pwm_freq_hz     = 0;
    }
#endif
#endif
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
        if (o.protocol == LED_RELAY) {
            out["relay_thr"] = o.relay_threshold;
        } else if (o.protocol == LED_PWM) {
            out["pwm_freq"]  = o.pwm_freq_hz;
            out["pwm_curve"] = o.pwm_curve;
            out["pwm_16bit"] = o.pwm_16bit;
            out["pwm_inv"]   = o.pwm_invert;
        } else {
            char order_str[5];
            color_order_to_str(o.color_order, led_ch_per_pixel(o.protocol), order_str);
            out["order"] = order_str;
        }
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
        // PWM fields: default 0 if absent (disables PWM when loading old configs)
        o.pwm_freq_hz     = out["pwm_freq"]   | (uint16_t)0;
        o.pwm_curve       = out["pwm_curve"]  | (uint8_t)0;
        o.pwm_16bit       = out["pwm_16bit"]  | (uint8_t)0;
        o.pwm_invert      = out["pwm_inv"]    | (uint8_t)0;
        o.relay_threshold = out["relay_thr"]  | (uint8_t)128;
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

void fixtureGetDmxMap(JsonObject& map) {
    for (int i = 0; i < ELYON_NUM_OUTPUTS; i++) {
        const elyon_output_cfg_t& out = elyonConfig.outputs[i];
        if (elyon_dmx_slots(&out) == 0) continue;
        uint8_t ch = led_ch_per_pixel(out.protocol);
        uint16_t remaining = (uint16_t)elyon_dmx_channels(&out);
        uint16_t u = out.universe_start;
        uint16_t pos = out.dmx_start;
        while (remaining > 0) {
            char uKey[8];
            snprintf(uKey, sizeof(uKey), "%u", u);
            JsonArray arr = map.containsKey(uKey)
                ? map[uKey].as<JsonArray>()
                : map.createNestedArray(uKey);
            uint16_t end = (pos + remaining - 1 > 512) ? 512 : (pos + remaining - 1);
            JsonArray range = arr.createNestedArray();
            range.add(pos);
            range.add(end);
            remaining -= (end - pos + 1);
            u++;
            pos = 1;
        }
    }
}

#endif // RAVLIGHT_FIXTURE_ELYON
