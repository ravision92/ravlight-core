#ifdef RAVLIGHT_MODULE_RECORDER

#ifndef RAVLIGHT_MODULE_DMX_PHYSICAL
  #error "RAVLIGHT_MODULE_RECORDER requires RAVLIGHT_MODULE_DMX_PHYSICAL"
#endif

#include "config.h"
#include <LittleFS.h>
#include <esp_log.h>
#include <string.h>
#include "dmx_manager.h"

#define TAG          "REC"
#define DMX_CHANNELS 512
#define SCENE_COUNT  4
#define SCENE_DURATION_MS 10000
#define SCENE_FPS    40
#define FRAME_DELAY_MS (1000 / SCENE_FPS)
#define SceneFrameCount (SCENE_FPS * (SCENE_DURATION_MS / 1000))

// Binary scene file layout:
//   [32 bytes]  scene name
//   [4 bytes]   frame count (uint32_t LE)
//   [2 bytes]   channel count (uint16_t LE)
//   [N × 512]   raw DMX frames

static TaskHandle_t recSceneTaskHandle  = NULL;
static TaskHandle_t autoSceneTaskHandle = NULL;
static volatile bool stopAutoScene_flag = false;
static volatile bool stopRec_flag       = false;
bool isRecording = false;

// ── Record ───────────────────────────────────────────────────────────────────

static void recordSceneTask(void* parameter) {
    int sceneIndex = (int)(intptr_t)parameter;
    char fileName[32];
    snprintf(fileName, sizeof(fileName), "/scene_%d.bin", sceneIndex);

    File file = LittleFS.open(fileName, FILE_WRITE);
    if (!file) {
        ESP_LOGE(TAG, "Cannot open %s for write", fileName);
        recSceneTaskHandle = NULL;
        vTaskDelete(NULL);
        return;
    }

    char sceneName[32] = {};
    snprintf(sceneName, sizeof(sceneName), "Scene %d", sceneIndex + 1);
    file.write((uint8_t*)sceneName, 32);

    uint32_t frameCount = SceneFrameCount;
    file.write((uint8_t*)&frameCount, sizeof(frameCount));

    uint16_t channels = DMX_CHANNELS;
    file.write((uint8_t*)&channels, sizeof(channels));

    ESP_LOGI(TAG, "Recording scene %d (%u frames @ %d fps)…", sceneIndex, frameCount, SCENE_FPS);

    uint8_t frameBuf[DMX_CHANNELS];
    for (uint32_t i = 0; i < frameCount && !stopRec_flag; i++) {
        if (xSemaphoreTake(dmxBufferMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            memcpy(frameBuf, dmxBuffer + 1, DMX_CHANNELS);
            xSemaphoreGive(dmxBufferMutex);
        }
        file.write(frameBuf, DMX_CHANNELS);
        vTaskDelay(pdMS_TO_TICKS(FRAME_DELAY_MS));
    }

    file.close();
    isRecording = false;
    ESP_LOGI(TAG, "Scene %d recorded", sceneIndex);
    recSceneTaskHandle = NULL;
    vTaskDelete(NULL);
}

void startSceneRecording(int sceneIndex) {
    if (sceneIndex < 0 || sceneIndex >= SCENE_COUNT) return;
    if (recSceneTaskHandle != NULL) {
        ESP_LOGW(TAG, "Already recording — stop first");
        return;
    }
    stopRec_flag = false;
    isRecording  = true;
    xTaskCreate(recordSceneTask, "RecScene", 4096, (void*)(intptr_t)sceneIndex, 1, &recSceneTaskHandle);
}

// ── Auto Scene (loop playback — activated by dmxInput == AUTO_SCENE) ─────────

static void autoSceneTask(void* parameter) {
    int sceneIndex = (int)(intptr_t)parameter;
    char fileName[32];
    snprintf(fileName, sizeof(fileName), "/scene_%d.bin", sceneIndex);

    ESP_LOGI(TAG, "Auto scene loop starting: slot %d", sceneIndex);
    handleDMXenable = false;

    uint8_t frameBuf[DMX_CHANNELS];

    while (!stopAutoScene_flag) {
        File file = LittleFS.open(fileName, FILE_READ);
        if (!file) {
            ESP_LOGW(TAG, "Scene %d not found — retrying in 1s", sceneIndex);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        char sceneName[32];
        file.read((uint8_t*)sceneName, 32);
        sceneName[31] = '\0';

        uint32_t frameCount;
        file.read((uint8_t*)&frameCount, sizeof(frameCount));

        uint16_t dmxChannels;
        file.read((uint8_t*)&dmxChannels, sizeof(dmxChannels));

        for (uint32_t i = 0; i < frameCount && !stopAutoScene_flag; i++) {
            if (file.read(frameBuf, DMX_CHANNELS) < DMX_CHANNELS) {
                ESP_LOGW(TAG, "Incomplete frame at %u — restarting loop", i);
                break;
            }
            if (xSemaphoreTake(dmxBufferMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                memcpy(dmxBuffer + 1, frameBuf, DMX_CHANNELS);
                xSemaphoreGive(dmxBufferMutex);
            }
            vTaskDelay(pdMS_TO_TICKS(FRAME_DELAY_MS));
        }
        file.close();
    }

    handleDMXenable = true;
    autoSceneTaskHandle = NULL;
    ESP_LOGI(TAG, "Auto scene loop stopped");
    vTaskDelete(NULL);
}

void startAutoScene(int sceneIndex) {
    if (sceneIndex < 0 || sceneIndex >= SCENE_COUNT) return;
    if (autoSceneTaskHandle != NULL) {
        stopAutoScene_flag = true;
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    stopAutoScene_flag = false;
    xTaskCreate(autoSceneTask, "AutoScene", 4096, (void*)(intptr_t)sceneIndex, 2, &autoSceneTaskHandle);
}

void stopAutoScene() {
    if (autoSceneTaskHandle == NULL) return;
    stopAutoScene_flag = true;
    // handleDMXenable is restored inside the task before it exits
}

#endif // RAVLIGHT_MODULE_RECORDER
