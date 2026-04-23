#ifdef RAVLIGHT_MODULE_RECORDER

#ifndef RAVLIGHT_MODULE_DMX_PHYSICAL
  #error "RAVLIGHT_MODULE_RECORDER requires RAVLIGHT_MODULE_DMX_PHYSICAL"
#endif

#include "config.h"
#include "SPIFFS.h"
#include "dmx_manager.h"

#define DMX_CHANNELS 512
#define SCENE_COUNT 4
#define SCENE_DURATION 10000  // scene duration in milliseconds
#define SCENE_FPS 40          // sampling rate (frames per second)
#define SceneFrameCount (SCENE_FPS * (SCENE_DURATION / 1000))

/*
 * First recorder implementation — kept for reference.
 * New implementation below uses playSceneTask with a binary header format.
 *
 * void recordSceneTask(void* parameter) {
 *     int sceneIndex = (int)parameter;
 *     String fileName = "/scene_" + String(sceneIndex) + ".bin";
 *     File file = SPIFFS.open(fileName, FILE_WRITE);
 *     ...
 * }
 */

TaskHandle_t playSceneTaskHandle = NULL;
bool stopScene = false;

// TODO: implement startSceneRecording — sample dmxBuffer to SPIFFS at SCENE_FPS
void startSceneRecording(int sceneIndex) {
    if (sceneIndex < 0 || sceneIndex >= SCENE_COUNT) return;
    Serial.printf("[REC] startSceneRecording(%d) — not yet implemented\n", sceneIndex);
}

void playScene(int sceneIndex) {
    if (sceneIndex < 0 || sceneIndex >= SCENE_COUNT) return;
    // xTaskCreate(playSceneTask, "PlayScene", 8192, (void*)sceneIndex, 1, NULL);
}

void sendDMX(uint8_t* data, int length) {
    Serial.print("[REC] DMX output: ");
    for (int i = 0; i < 10; i++) {
        Serial.print(data[i]);
        Serial.print(" ");
    }
    Serial.println("...");
}

void playSceneTask(void* parameter) {
    int sceneIndex = (int)parameter;
    String fileName = "/scene_" + String(sceneIndex) + ".bin";
    File file = SPIFFS.open(fileName, FILE_READ);

    if (!file) {
        Serial.printf("[REC] Failed to open scene file %d\n", sceneIndex);
        vTaskDelete(NULL);
    }

    char sceneName[32];
    file.read((uint8_t*)sceneName, 32);
    sceneName[31] = '\0';

    uint32_t frameCount;
    file.read((uint8_t*)&frameCount, sizeof(frameCount));

    uint16_t dmxChannels;
    file.read((uint8_t*)&dmxChannels, sizeof(dmxChannels));

    Serial.printf("[REC] Playing scene: %s (%d frames, %d channels)\n", sceneName, frameCount, dmxChannels);

    for (uint32_t i = 0; i < frameCount && !stopScene; i++) {
        size_t bytesRead = file.read(dmxBuffer, DMX_CHANNELS);

        if (bytesRead < DMX_CHANNELS) {
            Serial.println("[REC] Incomplete frame — stopping");
            break;
        }

        sendDMX(dmxBuffer, DMX_CHANNELS);
        vTaskDelay(1000 / SCENE_FPS / portTICK_PERIOD_MS);
    }

    file.close();
    Serial.println("[REC] Scene playback finished");
    vTaskDelete(NULL);
}

void startScenePlayback(int sceneIndex) {
    stopScene = false;

    if (playSceneTaskHandle != NULL) {
        Serial.println("[REC] Error: a scene is already playing");
        return;
    }

    xTaskCreate(playSceneTask, "PlayScene", 8192, (void*)sceneIndex, 1, &playSceneTaskHandle);
}

void stopScenePlayback() {
    stopScene = true;
    if (playSceneTaskHandle != NULL) {
        vTaskDelete(playSceneTaskHandle);
        playSceneTaskHandle = NULL;
    }
    Serial.println("[REC] Scene stopped");
}

#endif // RAVLIGHT_MODULE_RECORDER
