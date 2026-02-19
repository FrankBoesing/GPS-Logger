#include "ota.h"

#ifdef ENABLE_OTA

#include "includes.h"
#include <ArduinoOTA.h>

#pragma GCC push_options
#pragma GCC optimize ("Os")

void initOTA()
{
    ArduinoOTA.setHostname(HOSTNAME);
/*
    ArduinoOTA.onStart([]()
                       { logi("OTA Start"); });

    ArduinoOTA.onEnd([]()
                     { logi("OTA End"); });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
                           { logi("OTA Progress: %u%%", (unsigned int)((progress / (float)total) * 100)); });

    ArduinoOTA.onError([](ota_error_t error)
                       { loge("OTA Error: %u", (unsigned)error); });
*/
    ArduinoOTA.begin();
//    logi("OTA ready.");

}

void handleOTA()
{
    ArduinoOTA.handle();
}

#else

void initOTA() {}
void handleOTA() {}

#endif
