#include "core/stats.h"
#include "dmx_manager.h"
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <ArduinoJson.h>

#if defined(RAVLIGHT_FIXTURE_VEYRON)
  #include "fixtures/veyron/fixture.h"
#elif defined(RAVLIGHT_FIXTURE_ELYON)
  #include "fixtures/elyon/fixture.h"
#elif defined(RAVLIGHT_FIXTURE_ORION)
  #include "fixtures/orion/fixture.h"
#endif

#ifndef PROJECT_NAME
#define PROJECT_NAME "Unknown"
#endif

// ── Render counters (Elyon / Veyron / Orion render task) ────────────────────

static uint32_t s_frames           = 0;
static uint64_t s_render_us_sum    = 0;
static uint32_t s_render_us_max    = 0;
static uint64_t s_mutex_us_sum     = 0;
static uint32_t s_mutex_us_max     = 0;
static uint32_t s_mutex_samples    = 0;
static uint32_t s_min_free_heap    = 0;
static uint32_t s_reset_uptime_ms  = 0;

// Per-call timestamps (single-threaded render → no race; one render task).
static int64_t  s_frame_start_us   = 0;
static int64_t  s_mutex_wait_us    = 0;

void stats_render_frame_start() {
    s_frame_start_us = esp_timer_get_time();
}

void stats_render_mutex_wait_start() {
    s_mutex_wait_us = esp_timer_get_time();
}

void stats_render_mutex_wait_end() {
    uint32_t w = (uint32_t)(esp_timer_get_time() - s_mutex_wait_us);
    s_mutex_us_sum += w;
    if (w > s_mutex_us_max) s_mutex_us_max = w;
    s_mutex_samples++;
}

void stats_render_frame_end() {
    if (s_frame_start_us == 0) return;
    uint32_t r = (uint32_t)(esp_timer_get_time() - s_frame_start_us);
    s_render_us_sum += r;
    if (r > s_render_us_max) s_render_us_max = r;
    s_frames++;

    uint32_t heap = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    if (s_min_free_heap == 0 || heap < s_min_free_heap) s_min_free_heap = heap;
}

void stats_reset() {
    s_frames          = 0;
    s_render_us_sum   = 0;
    s_render_us_max   = 0;
    s_mutex_us_sum    = 0;
    s_mutex_us_max    = 0;
    s_mutex_samples   = 0;
    s_min_free_heap   = 0;
    s_reset_uptime_ms = millis();
}

String stats_to_json() {
    StaticJsonDocument<512> doc;
    uint32_t now_ms = millis();
    uint32_t window_ms = (s_reset_uptime_ms > 0) ? (now_ms - s_reset_uptime_ms) : now_ms;

    doc["fixture"]    = PROJECT_NAME;
    doc["uptime_ms"]  = now_ms;
    doc["window_ms"]  = window_ms;

    JsonObject render = doc.createNestedObject("render");
    render["frames"]   = s_frames;
    render["render_us_avg"] = s_frames ? (uint32_t)(s_render_us_sum / s_frames) : 0;
    render["render_us_max"] = s_render_us_max;
    render["fps"] = (window_ms > 0)
                  ? (uint32_t)((uint64_t)s_frames * 1000 / window_ms)
                  : 0;

    JsonObject mtx = doc.createNestedObject("mutex_wait");
    mtx["samples"]   = s_mutex_samples;
    mtx["us_avg"]    = s_mutex_samples ? (uint32_t)(s_mutex_us_sum / s_mutex_samples) : 0;
    mtx["us_max"]    = s_mutex_us_max;

    JsonObject net = doc.createNestedObject("network");
    net["artnet_packets"]  = artnetPacketCount();
    net["artsync_packets"] = artsyncPacketCount();

    JsonObject mem = doc.createNestedObject("memory");
    mem["free_heap_internal"] = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    mem["free_heap_largest"]  = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    mem["min_free_heap"]      = s_min_free_heap;

    String out;
    serializeJson(doc, out);
    return out;
}
