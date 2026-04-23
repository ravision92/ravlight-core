#ifndef DMX_RECORDER_H
#define DMX_RECORDER_H

#ifdef RAVLIGHT_MODULE_RECORDER

void setupDMXRecorder();
void startSceneRecording(int sceneIndex);
void recordSceneTask(void* parameter);
void playScene(int sceneIndex);
void playSceneTask(void* parameter);

#endif // RAVLIGHT_MODULE_RECORDER
#endif // DMX_RECORDER_H
