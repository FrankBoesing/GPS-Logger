#ifndef UTILS_H
#define UTILS_H

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "config.h"
#include "debug.h"

#define FILE_PREFIX "/"
#define FILE_SUFFIX ".bin"

void initTimeOffset();
time_t timegm(struct tm *tm);
extern const long& offset_seconds;

bool str_to_ll(const char *str, long long *out);

//bool endsWith(const char *str, const char *suffix);
String findFile(const bool newest, const char *fileext = FILE_SUFFIX);

void readFileList(JsonObject& fileList,  const char *fileext = FILE_SUFFIX);
int deleteFiles(const char *filename);

bool isGPSConnected();
[[noreturn]] void error(const char *msg);
#endif
