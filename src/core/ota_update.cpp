#include "core/ota_update.h"
#include "version.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <ArduinoJson.h>
#include <esp_log.h>

static const char* TAG = "OTA";

// custom_fw_name of this build (e.g. "elyon_quinled_octa"), injected as a
// -D flag by scripts/embed_assets.py. Guard in case it is ever missing.
#ifndef RAVLIGHT_FW_BASE
#define RAVLIGHT_FW_BASE "unknown"
#endif

static const char* FEED_HOST = "ravlight.com";
static OtaState g_ota;

// ── semver compare: returns >0 if a>b, 0 if equal, <0 if a<b ────────────────
static int semverCmp(const char* a, const char* b) {
    int a0=0,a1=0,a2=0, b0=0,b1=0,b2=0;
    sscanf(a, "%d.%d.%d", &a0,&a1,&a2);
    sscanf(b, "%d.%d.%d", &b0,&b1,&b2);
    if (a0 != b0) return a0 - b0;
    if (a1 != b1) return a1 - b1;
    return a2 - b2;
}

// ── manifest check task ─────────────────────────────────────────────────────
static void checkTask(void* arg) {
    // arg != nullptr → boot check: wait for the network to settle first.
    if (arg) vTaskDelay(pdMS_TO_TICKS(8000));

    g_ota.checking = true;
    g_ota.error[0] = 0;

    const String url = String("https://") + FEED_HOST +
                       "/firmware/" + RAVLIGHT_FW_BASE + "-update.json";
    bool ok = false;
    int  lastCode = 0;

    // The HTTPS handshake needs two ~16 KB *contiguous* mbedTLS buffers. On a
    // heap that has fragmented after long uptime these can fail to allocate
    // even on Ethernet (plenty of total free heap, no contiguous block), so a
    // single attempt is unreliable for a user-triggered check. Retry a few
    // times with a fresh client each round — the allocation is probabilistic
    // against fragmentation, and the short pause lets other tasks free memory,
    // so a retry frequently lands where the first try missed. The boot check
    // (clean heap) almost always succeeds first try.
    const int MAX_TRIES = 3;
    for (int attempt = 1; attempt <= MAX_TRIES && !ok; attempt++) {
        WiFiClientSecure client;   // fresh each round → TLS buffers re-allocated
        client.setInsecure();      // phase 1: HTTPS without cert pinning (see plan)
        client.setTimeout(8);
        // Fail a stalled TLS handshake fast instead of blocking on the ~120 s
        // default — otherwise the UI "Checking…" spinner hangs for minutes.
        client.setHandshakeTimeout(12);

        HTTPClient http;
        if (http.begin(client, url)) {
            int code = http.GET();
            lastCode = code;
            if (code == 200) {
                StaticJsonDocument<512> doc;
                if (deserializeJson(doc, http.getString()) == DeserializationError::Ok) {
                    const char* ver   = doc["version"] | "";
                    const char* notes = doc["notes"]   | "";
                    strlcpy(g_ota.latest, ver,   sizeof(g_ota.latest));
                    strlcpy(g_ota.notes,  notes, sizeof(g_ota.notes));
                    g_ota.available = semverCmp(g_ota.latest, g_ota.current) > 0;
                    g_ota.checked = true;
                    ok = true;
                    ESP_LOGI(TAG, "check: current=%s latest=%s available=%d (try %d)",
                             g_ota.current, g_ota.latest, g_ota.available, attempt);
                }
            }
            http.end();
        }
        if (!ok && attempt < MAX_TRIES) {
            ESP_LOGW(TAG, "check try %d failed (code=%d), retrying", attempt, lastCode);
            vTaskDelay(pdMS_TO_TICKS(1500));
        }
    }

    if (!ok) {
        if (lastCode < 0)
            // Negative = transport failure (couldn't connect / TLS handshake
            // failed), not an HTTP status. Usually low memory or a weak link.
            strlcpy(g_ota.error, "can't reach update server (signal/memory) — try Ethernet",
                    sizeof(g_ota.error));
        else if (lastCode > 0)
            snprintf(g_ota.error, sizeof(g_ota.error), "feed HTTP %d", lastCode);
        else
            strlcpy(g_ota.error, "feed connect failed", sizeof(g_ota.error));
    }
    g_ota.checking = false;
    vTaskDelete(nullptr);
}

// ── download + flash task ───────────────────────────────────────────────────
static void updateTask(void*) {
    g_ota.progress = 0;
    g_ota.error[0] = 0;

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(20);
    client.setHandshakeTimeout(15);

    // App image (the _fw_ binary — carries the embedded web UI). One file.
    String url = String("https://") + FEED_HOST +
                 "/firmware/" + RAVLIGHT_FW_BASE + "_fw.bin";

    httpUpdate.rebootOnUpdate(true);
    httpUpdate.onProgress([](int cur, int total) {
        if (total > 0) g_ota.progress = (int8_t)((int64_t)cur * 100 / total);
    });

    ESP_LOGW(TAG, "pulling update from %s", url.c_str());
    t_httpUpdate_return ret = httpUpdate.update(client, url, FW_VERSION);
    switch (ret) {
        case HTTP_UPDATE_OK:
            g_ota.progress = -3;   // success — device reboots via rebootOnUpdate
            break;
        case HTTP_UPDATE_NO_UPDATES:
            g_ota.progress = -1;
            strlcpy(g_ota.error, "server reports no update", sizeof(g_ota.error));
            break;
        default:
            g_ota.progress = -2;
            snprintf(g_ota.error, sizeof(g_ota.error), "update failed: %s",
                     httpUpdate.getLastErrorString().c_str());
            ESP_LOGE(TAG, "%s", g_ota.error);
            break;
    }
    vTaskDelete(nullptr);
}

void otaInit() {
    strlcpy(g_ota.current, FW_VERSION, sizeof(g_ota.current));
    // First check ~8 s after boot (arg != nullptr), off the critical path.
    if (!g_ota.checking)
        xTaskCreate(checkTask, "ota_boot", 6144, (void*)1, 3, nullptr);
}

void otaCheck() {
    if (g_ota.checking) return;
    xTaskCreate(checkTask, "ota_check", 6144, nullptr, 3, nullptr);
}

void otaStartUpdate() {
    if (!g_ota.available || g_ota.progress >= 0) return;
    xTaskCreate(updateTask, "ota_update", 8192, nullptr, 4, nullptr);
}

const OtaState& otaGetState() { return g_ota; }
