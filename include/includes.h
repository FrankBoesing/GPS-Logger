#ifndef INCLUDES_H
#define INCLUDES_H

#include <climits>
#include <atomic>
#include <vector>
#include "sortedStaticArray.hpp"

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <esp_wifi.h>
#include <time.h>
#include <stdlib.h>

#include "config.h"
#include "logfile.h"
#include "utils.h"
#include "gps_hw.h"
#include "gps.h"
#include "web.h"
#include "main.h"


// --- Externals ---

extern std::atomic<log_mode_t> logMode;
extern std::atomic<bool> logAppend;
extern std::atomic<log_cmd_t> logCmd;

extern ulong firstFix;
extern gps_state_ctx_t gps_state;
extern logfileW logfile;
extern const size_t &fsTotalBytes;
extern std::vector<wifiCredentials_t> wifiCreds;

#endif
