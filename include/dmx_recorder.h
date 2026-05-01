#ifndef DMX_RECORDER_H
#define DMX_RECORDER_H

#ifdef RAVLIGHT_MODULE_RECORDER

void startSceneRecording(int sceneIndex);
void startAutoScene(int sceneIndex);
void stopAutoScene();
extern bool isRecording;

#endif // RAVLIGHT_MODULE_RECORDER
#endif // DMX_RECORDER_H
