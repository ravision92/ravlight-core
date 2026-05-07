#pragma once
#include <ESPAsyncWebServer.h>

// Unified fixture webserver interface — each fixture implements all of these in
// fixtures/<name>/webserver.cpp.  No #ifdef RAVLIGHT_FIXTURE_* needed in core code.

// Streaming template substitution (called per-placeholder during chunked response)
void writeFixtureVars(String& out, const char* var);

// One-shot HTML placeholder injection (used for in-memory response path)
void injectFixturePlaceholders(String& html);

// Handle fixture-specific form fields from POST /save
void handleFixtureSaveParams(AsyncWebServerRequest* request, bool& needsRestart);

// Register fixture-specific HTTP routes (e.g. /highlight, /scene)
void registerFixtureRoutes(AsyncWebServer& server);
