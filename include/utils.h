#ifndef UTILS_H
#define UTILS_H

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "config.h"
#include "debug.h"

void initTimeOffset();
time_t timegm(struct tm *tm);
extern const long& offset_seconds;

//bool endsWith(const char *str, const char *suffix);
String findFile(const bool newest, const char *fileext);

void readFileList(JsonObject& fileList);
int deleteFiles(const char *filename);

#endif
