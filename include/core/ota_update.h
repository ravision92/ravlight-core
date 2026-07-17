#pragma once
#include <Arduino.h>

// Proprietary OTA update — pull model (ravlight.com feed).
//
// The device checks a per-board manifest on ravlight.com for the latest
// release, shows "update available" in the web UI, and — on operator request
// — downloads the app image (the _fw_ binary, which carries the embedded web
// UI) straight onto the inactive OTA slot via HTTPUpdate, then reboots. The
// partition scheme (app0/app1 + otadata) gives automatic rollback if the new
// image fails to boot.
//
// Policy: manual with notification (auto-update opt-in via config). Feed
// carries only the latest version + a short note — no version history.

struct OtaState {
    bool     checked      = false;   // a check has completed at least once
    bool     available    = false;   // latest > current
    bool     checking     = false;   // a check is in flight
    int8_t   progress      = -1;     // -1 idle, 0..100 downloading, -2 error, -3 done(reboot)
    char     current[16]  = {0};     // running FW_VERSION
    char     latest[16]   = {0};     // manifest version
    char     notes[160]   = {0};     // short release description
    char     error[80]    = {0};
};

// Call once after the network is up.
void otaInit();

// Trigger a manifest check (non-blocking — runs on a short-lived task).
// Also called automatically by otaInit() shortly after boot.
void otaCheck();

// Begin the download+flash of the available update (non-blocking task).
// No-op if no update is available or one is already running.
void otaStartUpdate();

const OtaState& otaGetState();
