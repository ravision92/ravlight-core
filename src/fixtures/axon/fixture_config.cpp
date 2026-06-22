#ifdef RAVLIGHT_FIXTURE_AXON
#include "fixture_config.h"
#include "fixtures/axon/fixture.h"

AxonConfig axonConfig;

#ifdef AXON_HAS_LED
// Track whether the last deserialize touched fields that require a driver
// re-init (protocol / pixel count / backend / clock partner). Same pattern
// as Elyon's _elyon_needs_restart — read + cleared by fixtureApplyLive().
static bool _axon_needs_restart = false;
#endif

void fixtureConfigDefaults() {
    // Axon is fundamentally a network → RS-485 bridge. The bridge is wired
    // up in the core (dmx_manager) — Axon's job at config-defaults time is
    // just to make sure the bridge is on by default. We don't change
    // dmxConfig here directly: the core's deserializeDmx() runs after this
    // and would clobber anything we set; instead the build env enables
    // dmxOutputEnabled by default via the regular config flow.

#ifdef AXON_HAS_LED
    for (int i = 0; i < AXON_NUM_LED_OUTPUTS; i++) {
        led_output_cfg_t& o = axonConfig.ledOutputs[i];
        o.protocol         = LED_WS2812B;
        o.pixel_count      = 0;        // start disabled — opt in from UI
        o.universe_start   = 0;
        o.dmx_start        = 1;
        o.grouping         = 1;
        o.invert           = 0;
        o.brightness       = 255;
        o.color_order[0]   = 0;        // R
        o.color_order[1]   = 1;        // G
        o.color_order[2]   = 2;        // B
        o.color_order[3]   = 3;        // W
        o.pwm_freq_hz      = 0;
        o.pwm_curve        = 0;
        o.pwm_16bit        = 0;
        o.pwm_invert       = 0;
        o.relay_threshold  = 128;
        o.relay_invert     = 0;
        o.clock_partner_idx = 0xFF;
        o.gamma_x10        = 10;       // linear
#ifdef RAVLIGHT_MODULE_I2S_LED
        o.backend = (uint8_t)LED_BACKEND_I2S;
#else
        o.backend = (uint8_t)LED_BACKEND_RMT;
#endif
    }
#endif
}

void fixtureConfigSerialize(JsonObject& fix) {
#ifdef AXON_HAS_LED
    JsonArray outputs = fix.createNestedArray("outputs");
    for (int i = 0; i < AXON_NUM_LED_OUTPUTS; i++) {
        const led_output_cfg_t& o = axonConfig.ledOutputs[i];
        JsonObject out = outputs.createNestedObject();
        out["proto"] = (int)o.protocol;
        out["count"] = o.pixel_count;
        out["univ"]  = o.universe_start;
        out["ch"]    = o.dmx_start;
        out["group"] = o.grouping;
        out["inv"]   = o.invert;
        out["bri"]   = o.brightness;
        if (o.gamma_x10 != 10) out["gamma"] = o.gamma_x10;
        if (o.clock_partner_idx != 0xFF) out["clock_p"] = o.clock_partner_idx;
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
#else
    (void)fix;
#endif
}

void fixtureConfigDeserialize(const JsonObject& fix) {
#ifdef AXON_HAS_LED
    _axon_needs_restart = false;
    JsonArrayConst outputs = fix["outputs"].as<JsonArrayConst>();
    for (int i = 0; i < AXON_NUM_LED_OUTPUTS; i++) {
        led_output_cfg_t& o = axonConfig.ledOutputs[i];
        JsonObjectConst out = outputs[i].as<JsonObjectConst>();
        // Snapshot structural fields before overwriting so we can detect
        // changes that require a driver re-init.
        led_protocol_t prev_proto    = o.protocol;
        uint16_t       prev_count    = o.pixel_count;
        uint8_t        prev_backend  = o.backend;
        uint8_t        prev_partner  = o.clock_partner_idx;
        o.protocol         = (led_protocol_t)(out["proto"] | (int)LED_WS2812B);
        o.pixel_count      = out["count"] | (uint16_t)0;
        o.universe_start   = out["univ"]  | (uint16_t)0;
        o.dmx_start        = out["ch"]    | (uint16_t)1;
        o.grouping         = out["group"] | (uint8_t)1;
        o.invert           = out["inv"]   | (uint8_t)0;
        o.brightness       = out["bri"]   | (uint8_t)255;
        o.gamma_x10        = out["gamma"] | (uint8_t)10;
        if (o.gamma_x10 < 10) o.gamma_x10 = 10;
        if (o.gamma_x10 > 30) o.gamma_x10 = 30;
        if (o.grouping == 0) o.grouping = 1;
        o.pwm_freq_hz      = out["pwm_freq"]   | (uint16_t)0;
        o.pwm_curve        = out["pwm_curve"]  | (uint8_t)0;
        o.pwm_16bit        = out["pwm_16bit"]  | (uint8_t)0;
        o.pwm_invert       = out["pwm_inv"]    | (uint8_t)0;
        o.relay_threshold  = out["relay_thr"]  | (uint8_t)128;
        o.relay_invert     = out["relay_inv"]  | (uint8_t)0;
        o.clock_partner_idx = out["clock_p"]   | (uint8_t)0xFF;
#ifdef RAVLIGHT_MODULE_I2S_LED
        o.backend          = out["be"]         | (uint8_t)LED_BACKEND_I2S;
#else
        o.backend          = (uint8_t)LED_BACKEND_RMT;
#endif
        const char* order_str = out["order"] | "";
        if (order_str[0]) {
            color_order_from_str(order_str, o.color_order);
        } else {
            o.color_order[0] = 0; o.color_order[1] = 1;
            o.color_order[2] = 2; o.color_order[3] = 3;
        }
        if (o.protocol != prev_proto || o.pixel_count != prev_count ||
            o.backend != prev_backend || o.clock_partner_idx != prev_partner) {
            _axon_needs_restart = true;
        }
    }
#else
    (void)fix;
#endif
}

bool fixtureApplyLive() {
#ifdef AXON_HAS_LED
    bool need = _axon_needs_restart;
    _axon_needs_restart = false;
    return need;
#else
    return false;
#endif
}

void fixtureGetDmxMap(JsonObject& map) {
#ifdef AXON_HAS_LED
    for (int i = 0; i < AXON_NUM_LED_OUTPUTS; i++) {
        const led_output_cfg_t& out = axonConfig.ledOutputs[i];
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
#else
    (void)map;
#endif
}

#endif // RAVLIGHT_FIXTURE_AXON
