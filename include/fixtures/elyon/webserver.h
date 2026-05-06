#pragma once
#ifdef RAVLIGHT_FIXTURE_ELYON
#include <ESPAsyncWebServer.h>
#include "fixtures/elyon/dmx_fixture.h"

void registerElyonRoutes(AsyncWebServer& server);
void injectElyonPlaceholders(String& html);
void handleElyonSaveParams(AsyncWebServerRequest* request, bool& needsRestart);
void writeElyonVars(String& out, const char* var);

#endif // RAVLIGHT_FIXTURE_ELYON
