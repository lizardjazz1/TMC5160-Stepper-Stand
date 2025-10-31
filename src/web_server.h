#pragma once
#include <ESPAsyncWebServer.h>

extern AsyncWebServer server;

void init_web_server();
void handleClient();