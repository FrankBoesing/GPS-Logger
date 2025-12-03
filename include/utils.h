#ifndef UTILS_H
#define UTILS_H

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "config.h"
#include "logfile.h"
#include "debug.h"

#define MILLISECOND 1000UL
#define SECOND (1000UL * MILLISECOND)

enum eLogMode : uint8_t
{
  NoLog = 0,
  LogAfterBoot = 1,
  LogAfterMinSpeed = 2
};

enum eLogCmd: uint8_t
{
  nope = 0,
  stopNow = 1,
  startNow = 2
};

extern volatile eLogMode logMode;
extern volatile eLogCmd logCmd;

bool str_to_ll(const char *str, long long *out);

//bool endsWith(const char *str, const char *suffix);
bool findFile(const bool newest, char *filename, const size_t maxlen, time_t *lastWrite, const char *fileext = FILE_SUFFIX);

void readFileList(JsonObject& fileList,  const char *fileext = FILE_SUFFIX);
int deleteFiles(const char *filename);

bool isGPSConnected();
[[noreturn]] void error(const char *msg);
#endif
