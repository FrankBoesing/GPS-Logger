#pragma once
#include "includes.h"

void onWiFiEvent(arduino_event_id_t event);
void boost(const bool fast = false);
bool interval(uint32_t &last, const uint32_t interval_ms);
void loadPrefs();
void savePrefs();

int id2filename(const time_t id, char *buf, const size_t szbuf);

void filelistSetActive(File &f, bool active);

size_t readFileList(const char *fileext = FILE_SUFFIX);
size_t deleteAllFiles();
size_t deleteFile(const time_t id);

void cleanupStorage();
