#ifndef MAIN_H
#define MAIN_H

#include "includes.h"

#define MILLISECOND 1000UL // us
#define SECOND (1000UL * MILLISECOND)

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

struct wifiCredentials_t
{
	char ssid[24];
	char pass[24];
};

#endif
