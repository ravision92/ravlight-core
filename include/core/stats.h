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
// Nested phase timers give a per-phase breakdown of one frame:
//
//   frame_start
//     mutex_wait_start ── mutex_wait_end
//     compute_start ────── compute_end       (per-pixel math, LUT lookups)
//     flush_start ──────── flush_end         (kick off DMA / RMT transmission)
//     wait_start ───────── wait_end          (block until the wire is done)
//   frame_end
//
// All four inner phases are optional — if a fixture never calls the
// _start marker of a phase, the phase reads as 0 samples in /stats.
void     stats_render_frame_start();
void     stats_render_frame_end();
void     stats_render_mutex_wait_start();
void     stats_render_mutex_wait_end();
void     stats_render_compute_start();
void     stats_render_compute_end();
void     stats_render_flush_start();
void     stats_render_flush_end();
void     stats_render_wait_start();
void     stats_render_wait_end();

// Reset all accumulators (called by /stats/reset).
void     stats_reset();

// Serialise the current snapshot to JSON.
String   stats_to_json();
