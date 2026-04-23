#ifndef WEBSERVER_MANAGER_H
#define WEBSERVER_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include <FS.h> 
#include <ESPAsyncWebServer.h>


extern AsyncWebServer& getInstance();

String readFile(fs::FS &fs, const char *path);


void initWebServer();
void startWebServer();
void stopWebServer();
void handleWebRequests();
//void requestWiFiNetworks(AsyncWebServerRequest *request);
String requestWiFiNetworks();
void scheduleRestart();

#endif
