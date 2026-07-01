#include "core/stats.h"
#include "dmx_manager.h"
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <ArduinoJson.h>
#if defined(RAVLIGHT_FIXTURE_ELYON) && defined(RAVLIGHT_MODULE_I2S_LED)
#include "core/i2s_parallel_output.h"
#endif

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
static uint64_t s_compute_us_sum   = 0;
static uint32_t s_compute_us_max   = 0;
static uint32_t s_compute_samples  = 0;
static uint64_t s_flush_us_sum     = 0;
static uint32_t s_flush_us_max     = 0;
static uint32_t s_flush_samples    = 0;
static uint64_t s_wait_us_sum      = 0;
static uint32_t s_wait_us_max      = 0;
static uint32_t s_wait_samples     = 0;
static uint32_t s_min_free_heap    = 0;
static uint32_t s_reset_uptime_ms  = 0;

// Per-call timestamps (single-threaded render → no race; one render task).
static int64_t  s_frame_start_us   = 0;
static int64_t  s_mutex_wait_us    = 0;
static int64_t  s_compute_us       = 0;
static int64_t  s_flush_us         = 0;
static int64_t  s_wait_us          = 0;

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

void stats_render_compute_start() { s_compute_us = esp_timer_get_time(); }
void stats_render_compute_end()   {
    uint32_t d = (uint32_t)(esp_timer_get_time() - s_compute_us);
    s_compute_us_sum += d;
    if (d > s_compute_us_max) s_compute_us_max = d;
    s_compute_samples++;
}

void stats_render_flush_start() { s_flush_us = esp_timer_get_time(); }
void stats_render_flush_end()   {
    uint32_t d = (uint32_t)(esp_timer_get_time() - s_flush_us);
    s_flush_us_sum += d;
    if (d > s_flush_us_max) s_flush_us_max = d;
    s_flush_samples++;
}

void stats_render_wait_start() { s_wait_us = esp_timer_get_time(); }
void stats_render_wait_end()   {
    uint32_t d = (uint32_t)(esp_timer_get_time() - s_wait_us);
    s_wait_us_sum += d;
    if (d > s_wait_us_max) s_wait_us_max = d;
    s_wait_samples++;
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
    s_compute_us_sum  = 0;
    s_compute_us_max  = 0;
    s_compute_samples = 0;
    s_flush_us_sum    = 0;
    s_flush_us_max    = 0;
    s_flush_samples   = 0;
    s_wait_us_sum     = 0;
    s_wait_us_max     = 0;
    s_wait_samples    = 0;
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

    JsonObject cmp = doc.createNestedObject("compute");
    cmp["samples"]   = s_compute_samples;
    cmp["us_avg"]    = s_compute_samples ? (uint32_t)(s_compute_us_sum / s_compute_samples) : 0;
    cmp["us_max"]    = s_compute_us_max;

    JsonObject flh = doc.createNestedObject("flush_trigger");
    flh["samples"]   = s_flush_samples;
    flh["us_avg"]    = s_flush_samples ? (uint32_t)(s_flush_us_sum / s_flush_samples) : 0;
    flh["us_max"]    = s_flush_us_max;

    JsonObject wt = doc.createNestedObject("wait_done");
    wt["samples"]    = s_wait_samples;
    wt["us_avg"]     = s_wait_samples ? (uint32_t)(s_wait_us_sum / s_wait_samples) : 0;
    wt["us_max"]     = s_wait_us_max;

    JsonObject net = doc.createNestedObject("network");
    net["artnet_packets"]   = artnetPacketCount();
    net["artsync_packets"]  = artsyncPacketCount();
    net["sacnsync_packets"] = sacnsyncPacketCount();

#if defined(RAVLIGHT_FIXTURE_ELYON) && defined(RAVLIGHT_MODULE_I2S_LED)
    JsonObject i2s = doc.createNestedObject("i2s");
    i2s["max_per_ch"] = i2s_par_max_pixels();
    i2s["wire_bpp"]   = i2s_par_wire_bpp();
    i2s["n_channels"] = i2s_par_n_channels();
#endif

    JsonObject mem = doc.createNestedObject("memory");
    mem["free_heap_internal"] = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    mem["free_heap_largest"]  = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    mem["min_free_heap"]      = s_min_free_heap;

    String out;
    serializeJson(doc, out);
    return out;
}
