#ifndef WEB_H
#define WEB_H
#include "config.h"
#include <ESPAsyncWebServer.h>

void setupWebServer();
void uiSendJson(const bool firstRequest = false);

#endif
