#pragma once
#include "includes.h"

typedef enum : uint8_t
{
	NOLOG = 0,
	LOGAUTOSTART
} log_mode_t;

typedef enum : uint8_t
{
	NOPE = 0,
	STOPNOW,
	STARTNOW
} log_cmd_t;


#define WIFI_SSID_MAX_LEN 32
#define WIFI_PASS_MAX_LEN 64
struct wifiCredentials_t
{
	char ssid[WIFI_SSID_MAX_LEN];
	char pass[WIFI_PASS_MAX_LEN];
};
