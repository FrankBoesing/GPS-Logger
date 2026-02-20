#pragma once
#include "config.h"
#include <ESPAsyncWebServer.h>

void setupWebServer();
void uiSendJson(const bool fileList = true, const bool wifiCredentials = false, const bool staticData = false);
