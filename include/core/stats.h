#pragma once
// ── Lightweight runtime stats counters ──────────────────────────────────────
//
// Fixture render hot path calls stats_render_* with timestamps; the cost is
// ~10 ns per call (one esp_timer_get_time + one accumulator add). The
// expensive work — averaging, JSON serialization, vTaskGetRunTimeStats —
// happens only when a client GETs /stats, on the webserver task.
//
// Counters are cleared by POST /stats/reset so you can measure a clean
// before/after window when validating an optimization.

#include <stdint.h>
#include <Arduino.h>

// Render-frame timing — call these from the fixture's render task.
void     stats_render_frame_start();
void     stats_render_frame_end();
void     stats_render_mutex_wait_start();
void     stats_render_mutex_wait_end();

// Reset all accumulators (called by /stats/reset).
void     stats_reset();

// Serialise the current snapshot to JSON.
String   stats_to_json();
