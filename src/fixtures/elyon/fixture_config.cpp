#ifdef RAVLIGHT_FIXTURE_ELYON
#include "fixture_config.h"
#include "fixtures/elyon/fixture.h"

ElyonConfig elyonConfig;

void fixtureConfigDefaults() {
    for (int i = 0; i < ELYON_NUM_OUTPUTS; i++) {
        led_output_cfg_t& o = elyonConfig.outputs[i];
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
        o.relay_invert    = 0;
        o.clock_partner_idx = 0xFF;
        o.gamma_x10      = 10;        // γ 1.0 = linear, no correction (backward compat)
        // Default backend follows the build: I2S firmware boots every WS281x
        // output on I2S (preserves pre-bus-architecture behaviour); RMT-only
        // firmware ignores the field at runtime.
#ifdef RAVLIGHT_MODULE_I2S_LED
        o.backend = (uint8_t)LED_BACKEND_I2S;
#else
        o.backend = (uint8_t)LED_BACKEND_RMT;
#endif
    }

#ifdef BOARD_ELYON_PRESET_ALL_PWM
    // Apply board-specific preset: all outputs default to LED_PWM.
    // Runs on first boot (NVS empty) and factory reset.
    for (int i = 0; i < ELYON_NUM_OUTPUTS; i++) {
        led_output_cfg_t& o = elyonConfig.outputs[i];
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
        o.relay_invert    = 0;
        o.clock_partner_idx = 0xFF;
#ifdef RAVLIGHT_MODULE_I2S_LED
        o.backend = (uint8_t)LED_BACKEND_I2S;
#else
        o.backend = (uint8_t)LED_BACKEND_RMT;
#endif
    }
#ifdef BOARD_ELYON_RELAY_OUTPUT_IDX
    {
        led_output_cfg_t& r = elyonConfig.outputs[BOARD_ELYON_RELAY_OUTPUT_IDX];
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
        const led_output_cfg_t& o = elyonConfig.outputs[i];
        JsonObject out = outputs.createNestedObject();
        out["proto"] = (int)o.protocol;
        out["count"] = o.pixel_count;
        out["univ"]  = o.universe_start;
        out["ch"]    = o.dmx_start;
        out["group"] = o.grouping;
        out["inv"]   = o.invert;
        out["bri"]   = o.brightness;
        // Skip default gamma (10 = linear) to keep the config blob compact.
        if (o.gamma_x10 != 10) out["gamma"] = o.gamma_x10;
        // Clock partner index — only meaningful for clocked outputs and for the
        // FOLLOWER output they consume. Skip serializing 0xFF (default).
        if (o.clock_partner_idx != 0xFF) out["clock_p"] = o.clock_partner_idx;
        // backend: only serialise when it differs from the build default. RMT-only
        // firmware never writes the key; I2S firmware emits "be" only for RMT-mode
        // outputs (default = I2S, so I2S outputs are implicit).
#ifdef RAVLIGHT_MODULE_I2S_LED
        if (o.backend != (uint8_t)LED_BACKEND_I2S) out["be"] = o.backend;
#endif
        if (o.protocol == LED_RELAY) {
            out["relay_thr"] = o.relay_threshold;
            out["relay_inv"] = o.relay_invert;
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
        led_output_cfg_t& o = elyonConfig.outputs[i];
        JsonObjectConst out   = outputs[i].as<JsonObjectConst>();
        o.protocol       = (led_protocol_t)(out["proto"] | (int)LED_WS2812B);
        o.pixel_count    = out["count"] | (uint16_t)(i == 0 ? 30 : 0);
        o.universe_start = out["univ"]  | (uint16_t)i;
        o.dmx_start      = out["ch"]    | (uint16_t)1;
        o.grouping       = out["group"] | (uint8_t)1;
        o.invert         = out["inv"]   | (uint8_t)0;
        o.brightness     = out["bri"]   | (uint8_t)255;
        o.gamma_x10      = out["gamma"] | (uint8_t)10;
        if (o.gamma_x10 < 10) o.gamma_x10 = 10;        // clamp; never amplify below linear
        if (o.gamma_x10 > 30) o.gamma_x10 = 30;        // safety cap
        if (o.grouping == 0) o.grouping = 1;
        // PWM fields: default 0 if absent (disables PWM when loading old configs)
        o.pwm_freq_hz     = out["pwm_freq"]   | (uint16_t)0;
        o.pwm_curve       = out["pwm_curve"]  | (uint8_t)0;
        o.pwm_16bit       = out["pwm_16bit"]  | (uint8_t)0;
        o.pwm_invert      = out["pwm_inv"]    | (uint8_t)0;
        o.relay_threshold = out["relay_thr"]  | (uint8_t)128;
        o.relay_invert    = out["relay_inv"]  | (uint8_t)0;
        o.clock_partner_idx = out["clock_p"]  | (uint8_t)0xFF;
#ifdef RAVLIGHT_MODULE_I2S_LED
        o.backend           = out["be"]       | (uint8_t)LED_BACKEND_I2S;
#else
        o.backend           = (uint8_t)LED_BACKEND_RMT;
#endif
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

// All Elyon config changes (LED outputs) require RMT re-init → always restart.
bool fixtureApplyLive() { return true; }

void fixtureGetDmxMap(JsonObject& map) {
    for (int i = 0; i < ELYON_NUM_OUTPUTS; i++) {
        const led_output_cfg_t& out = elyonConfig.outputs[i];
        if (led_dmx_slots(&out) == 0) continue;
        uint8_t ch = led_ch_per_pixel(out.protocol);
        uint16_t remaining = (uint16_t)led_dmx_channels(&out);
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
