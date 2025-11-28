#ifndef UTILS_H
#define UTILS_H

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "config.h"
#include "debug.h"
#include "logfile.h"

void initTimeOffset();
time_t timegm(struct tm *tm);
extern const long& offset_seconds;

bool str_to_ll(const char *str, long long *out);

//bool endsWith(const char *str, const char *suffix);
bool findFile(const bool newest, char *filename, const size_t maxlen, time_t *lastWrite, const char *fileext = FILE_SUFFIX);

void readFileList(JsonObject& fileList,  const char *fileext = FILE_SUFFIX);
int deleteFiles(const char *filename);

bool isGPSConnected();
[[noreturn]] void error(const char *msg);
#endif
