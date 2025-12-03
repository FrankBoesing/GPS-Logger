#ifndef WEB_H
#define WEB_H
#include "config.h"

void setupWebServer();

size_t uiClientCount();
void uiSendEvent(String &payload);

#endif
