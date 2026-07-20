#include "core/ota_update.h"
#include "version.h"
#include <WiFiClient.h>
#include <HTTPClient.h>
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

    // Plain HTTP (no TLS) — deliberate. The manifest is public, non-secret
    // version info, and the firmware image is integrity-checked by the
    // bootloader hash regardless, so HTTPS bought nothing (we used setInsecure
    // anyway). Dropping TLS removes the ~40 KB *contiguous* mbedTLS buffers
    // that failed to allocate on a fragmented/WiFi heap ("SSL - Memory
    // allocation failed"), so the check now works on every board (incl. Orion
    // on WiFi), not just high-heap Ethernet ones. Requires "Enforce HTTPS"
    // OFF on the GitHub Pages site so http:// serves 200 (no 301 → https).
    const String url = String("http://") + FEED_HOST +
                       "/firmware/" + RAVLIGHT_FW_BASE + "-update.json";
    bool ok = false;
    int  lastCode = 0;

    const int MAX_TRIES = 2;   // light retry for transient network hiccups
    for (int attempt = 1; attempt <= MAX_TRIES && !ok; attempt++) {
        WiFiClient client;   // plain TCP — no mbedTLS, no big contiguous alloc
        client.setTimeout(8);

        HTTPClient http;
        http.setConnectTimeout(5000);
        if (http.begin(client, url)) {
            int code = http.GET();
            lastCode = code;
            if (code == 200) {
                StaticJsonDocument<512> doc;
                if (deserializeJson(doc, http.getString()) == DeserializationError::Ok) {
                    const char* ver   = doc["version"] | "";
                    const char* notes = doc["notes"]   | "";
                    const char* url   = doc["url"]     | "";
                    strlcpy(g_ota.latest, ver,   sizeof(g_ota.latest));
                    strlcpy(g_ota.notes,  notes, sizeof(g_ota.notes));
                    strlcpy(g_ota.url,    url,   sizeof(g_ota.url));
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
            // Negative = couldn't connect (no network / DNS / server down).
            strlcpy(g_ota.error, "can't reach update server — check network",
                    sizeof(g_ota.error));
        else if (lastCode > 0)
            snprintf(g_ota.error, sizeof(g_ota.error), "feed HTTP %d", lastCode);
        else
            strlcpy(g_ota.error, "feed connect failed", sizeof(g_ota.error));
    }
    g_ota.checking = false;
    vTaskDelete(nullptr);
}

// No on-device download task: the operator downloads the _fw_ image with their
// browser (g_ota.url) and flashes it via the manual-upload endpoint. This keeps
// the heap-hungry HTTPS transfer off the ESP32 — see the header note.

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

const OtaState& otaGetState() { return g_ota; }
