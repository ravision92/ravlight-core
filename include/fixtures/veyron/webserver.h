#pragma once
#ifdef RAVLIGHT_FIXTURE_VEYRON
#include <ESPAsyncWebServer.h>
#include "fixtures/veyron/dmx_fixture.h"

void registerVeyronRoutes(AsyncWebServer& server);
void injectVeyronPlaceholders(String& html);
void handleVeyronSaveParams(AsyncWebServerRequest* request, bool& needsRestart);
void writeVeyronVars(String& out, const char* var);

#endif // RAVLIGHT_FIXTURE_VEYRON
