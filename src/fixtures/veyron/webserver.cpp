#ifdef RAVLIGHT_FIXTURE_VEYRON
#include "fixture_webserver.h"
#include "fixtures/veyron/dmx_fixture.h"
#include "fixtures/veyron/fixture_html.h"
#include "fixtures/veyron/fixture.h"
#include "config.h"
#include <string.h>

// Forward declaration — writeFixtureVars and scanVeyronTemplate are mutually recursive
static void scanVeyronTemplate(String& out, const char* html, size_t len);

void writeFixtureVars(String& out, const char* var) {
    if (strcmp(var, "FIXTURE_SECTION") == 0) {
        scanVeyronTemplate(out, VEYRON_FIXTURE_HTML, sizeof(VEYRON_FIXTURE_HTML) - 1);
    } else if (strcmp(var, "FIXTURE_JS") == 0) {
        out.concat(VEYRON_FIXTURE_JS);
    } else if (strcmp(var, "fixture_display_name") == 0) {
        out.concat(VEYRON_FIXTURE_NAME);
    } else if (strcmp(var, "rgbw_start_address") == 0) {
        char b[8]; snprintf(b, sizeof(b), "%u", (unsigned)veyronConfig.rgbwStart); out.concat(b);
    } else if (strcmp(var, "strobe_start_address") == 0) {
        char b[8]; snprintf(b, sizeof(b), "%u", (unsigned)veyronConfig.strobeStart); out.concat(b);
    } else if (strcmp(var, "wh_start_address") == 0) {
        char b[8]; snprintf(b, sizeof(b), "%u", (unsigned)veyronConfig.whiteStart); out.concat(b);
    } else if (strncmp(var, "personality", 11) == 0 && strstr(var + 11, "_selected")) {
        int p = atoi(var + 11);
        if ((int)veyronConfig.personality == p) out.concat("selected");
    } else if (strcmp(var, "LINEAR") == 0) {
        if (veyronConfig.DimCurves == LINEAR)         out.concat("selected");
    } else if (strcmp(var, "SQUARE") == 0) {
        if (veyronConfig.DimCurves == SQUARE)         out.concat("selected");
    } else if (strcmp(var, "INVERSE_SQUARE") == 0) {
        if (veyronConfig.DimCurves == INVERSE_SQUARE) out.concat("selected");
    } else if (strcmp(var, "S_CURVE") == 0) {
        if (veyronConfig.DimCurves == S_CURVE)        out.concat("selected");
    }
}

static void scanVeyronTemplate(String& out, const char* html, size_t len) {
    const char* p   = html;
    const char* end = html + len;
    while (p < end) {
        const char* open = p;
        while (open < end && !(open[0] == '{' && open + 1 < end && open[1] == '{')) open++;
        if (open > p) out.concat((const char*)p, open - p);
        if (open >= end) break;
        const char* close = strstr(open + 2, "}}");
        if (!close) { out.concat((const char*)open, end - open); break; }
        size_t nameLen = (size_t)(close - open - 2);
        char varBuf[64] = {};
        if (nameLen < sizeof(varBuf)) memcpy(varBuf, open + 2, nameLen);
        writeFixtureVars(out, varBuf);
        p = close + 2;
    }
}

void injectFixturePlaceholders(String& html) {
    html.replace("{{FIXTURE_SECTION}}",       VEYRON_FIXTURE_HTML);
    html.replace("{{FIXTURE_JS}}",            VEYRON_FIXTURE_JS);
    html.replace("{{fixture_display_name}}",  VEYRON_FIXTURE_NAME);
    html.replace("{{rgbw_start_address}}",    String(veyronConfig.rgbwStart));
    html.replace("{{strobe_start_address}}",  String(veyronConfig.strobeStart));
    html.replace("{{wh_start_address}}",      String(veyronConfig.whiteStart));
    html.replace("{{personality1_selected}}", veyronConfig.personality == PERSONALITY_1 ? "selected" : "");
    html.replace("{{personality2_selected}}", veyronConfig.personality == PERSONALITY_2 ? "selected" : "");
    html.replace("{{personality3_selected}}", veyronConfig.personality == PERSONALITY_3 ? "selected" : "");
    html.replace("{{personality4_selected}}", veyronConfig.personality == PERSONALITY_4 ? "selected" : "");
    html.replace("{{personality5_selected}}", veyronConfig.personality == PERSONALITY_5 ? "selected" : "");
    html.replace("{{LINEAR}}",          veyronConfig.DimCurves == LINEAR         ? "selected" : "");
    html.replace("{{SQUARE}}",          veyronConfig.DimCurves == SQUARE         ? "selected" : "");
    html.replace("{{INVERSE_SQUARE}}",  veyronConfig.DimCurves == INVERSE_SQUARE ? "selected" : "");
    html.replace("{{S_CURVE}}",         veyronConfig.DimCurves == S_CURVE        ? "selected" : "");
}

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
