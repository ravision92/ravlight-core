#ifdef RAVLIGHT_FIXTURE_VEYRON
#include "fixture_webserver.h"
#include "fixtures/veyron/dmx_fixture.h"
#include "fixtures/veyron/fixture.h"
#include "config.h"
#include <string.h>

void handleFixtureSaveParams(AsyncWebServerRequest* request, bool& needsRestart) {
    if (request->hasParam("personality", true)) {
        setPersonality(static_cast<FixturePersonality>(
            request->getParam("personality", true)->value().toInt()));
    }
    if (request->hasParam("RGBWstartAddress", true) ||
        request->hasParam("WhStartAddress", true) ||
        request->hasParam("strobeStartAddress", true)) {
        int rgbw   = request->hasParam("RGBWstartAddress", true)
                     ? request->getParam("RGBWstartAddress", true)->value().toInt()
                     : veyronConfig.rgbwStart;
        int wh     = request->hasParam("WhStartAddress", true)
                     ? request->getParam("WhStartAddress", true)->value().toInt()
                     : veyronConfig.whiteStart;
        int strobe = request->hasParam("strobeStartAddress", true)
                     ? request->getParam("strobeStartAddress", true)->value().toInt()
                     : veyronConfig.strobeStart;
        setFixtureAddresses(rgbw, wh, strobe);
    }
    if (request->hasParam("dimCurves", true)) {
        setDimCurve(request->getParam("dimCurves", true)->value().toInt());
    }
}

void registerFixtureRoutes(AsyncWebServer& server) {
    server.on("/highlight", HTTP_POST, [](AsyncWebServerRequest* request) {
        startHighlight();
        request->send(200, "text/plain", "Highlight started");
    });
}

#endif // RAVLIGHT_FIXTURE_VEYRON
