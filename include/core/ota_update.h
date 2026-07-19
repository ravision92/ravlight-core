#pragma once
#include <Arduino.h>

// Proprietary OTA update — check-only model (ravlight.com feed).
//
// The device checks a per-board manifest on ravlight.com for the latest
// release and shows "update available" + a download link in the web UI. The
// operator downloads the app image (the _fw_ binary) with their browser and
// uploads it via the manual-upload endpoint. This deliberately keeps the
// heap-hungry HTTPS *download* off the device: mbedTLS needs ~40 KB contiguous
// heap, which fragments after uptime and fails the on-device pull; the tiny
// HTTPS *check* is fine (and retried). The manual upload is TLS-free on the
// device, so it always works. The app0/app1 + otadata scheme still gives
// automatic rollback if a flashed image fails to boot.

struct OtaState {
    bool     checked      = false;   // a check has completed at least once
    bool     available    = false;   // latest > current
    bool     checking     = false;   // a check is in flight
    char     current[16]  = {0};     // running FW_VERSION
    char     latest[16]   = {0};     // manifest version
    char     notes[160]   = {0};     // short release description
    char     url[128]     = {0};     // download URL of the latest _fw_ image
    char     error[80]    = {0};
};

// Call once after the network is up.
void otaInit();

// Trigger a manifest check (non-blocking — runs on a short-lived task).
// Also called automatically by otaInit() shortly after boot.
void otaCheck();

const OtaState& otaGetState();
