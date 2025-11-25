#include "utils.h"

SemaphoreHandle_t jsonMutex;

/****************************************************************************************************************************/
/****************************************************************************************************************************/

// ESP32 hat kein timegm implementiert. Reparatur:

static long _offset_seconds = 0;
const long& offset_seconds = _offset_seconds; //make it readonly

time_t timegm(struct tm *tm) // initTimeOffset muss vorher aufgerufen worden sein
{
  return mktime(tm) + offset_seconds;
}

void initTimeOffset() // setzt voraus, dass die lokale Zeitzone schon gesetzt wurde.
{
  time_t now = time(NULL);
  struct tm local_tm = *localtime(&now);
  struct tm utc_tm = *gmtime(&now);

  time_t local_epoch = mktime(&local_tm);
  time_t utc_epoch = mktime(&utc_tm);

  _offset_seconds = local_epoch - utc_epoch;
  log_v("TZ Offset: %d", offset_seconds);
}

/****************************************************************************************************************************/

// Fehlerbehandlung: Gibt eine Fehlermeldung aus und bleibt in einer Endlosschleife
void error(const char *msg)
{
  while (true) {
    log_e("Fehler: %s", msg);
    delay(2000);
  }
}

//Beim Booten prüfen ob GPS verbunden ist.
bool isGPSConnected()
{
  constexpr const unsigned long gpsConnectTimeout = 2000; // 2 Sekunden
  constexpr const unsigned long minNumChars = 20;

  unsigned long start = millis();

  while (GPSSerial.available() < minNumChars && millis()-start < gpsConnectTimeout)
  {
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }

  return GPSSerial.available() > 0;
}


/****************************************************************************************************************************/

static bool endsWith(const char *str, const char *suffix)
{
  if (!str || !suffix)
    return false;
  size_t lenstr = strlen(str);
  size_t lensuffix = strlen(suffix);
  if (lensuffix > lenstr)
    return false;
  return strncmp(str + lenstr - lensuffix, suffix, lensuffix) == 0;
}

/****************************************************************************************************************************/
// Refresh the file list and write as JSON to FILELIST_PATH
void readFileList(JsonObject &fileList)
{
  xSemaphoreTake(semFile, portMAX_DELAY);

  fileList.clear();
  fileList["sizePoint"] = sizeof(GPSPoint);

  JsonArray arr = fileList["files"].to<JsonArray>();

  File root = LittleFS.open("/");
  File f = root.openNextFile();
  while (f)
  {
    if (f.size() > 0)
    {
      JsonObject obj = arr.add<JsonObject>();
      obj["name"] = f.name();
      obj["len"] = f.size();
      //obj["lastWrite"] = f.getLastWrite();
      obj["active"] = (currentFile && strcmp(currentFile.name(), f.name()) == 0);
    }
    f.close();
    f = root.openNextFile();
  }
  root.close();

  xSemaphoreGive(semFile);
}

static int deleteAllFiles(const char *fileext)
{
  int count = 0;
  xSemaphoreTake(semFile, portMAX_DELAY);
  File root = LittleFS.open("/");
  File fnext = root.openNextFile();

  while (fnext)
  {
    if (endsWith(fnext.name(), fileext) && !(currentFile && strcmp(currentFile.name(), fnext.name()) == 0))
    {
      if (LittleFS.remove(fnext.name()))
      {
        count++;
        log_i("Lösche: %s", fnext.name());
      }
      else
      {
        log_w("Konnte %s nicht löschen", fnext.name());
      }
    }
    fnext = root.openNextFile();
  }

  root.close();
  xSemaphoreGive(semFile);
  return count;
}

static int deleteFile(const char *filename)
{
  int count = 0;
  xSemaphoreTake(semFile, portMAX_DELAY);
  if (LittleFS.exists(filename) && !(currentFile && strcmp(currentFile.name(), filename) == 0))
  {
    log_i("Lösche: %s", filename);
    if (LittleFS.remove(filename))
    {
      count = 1;
    }
    else
    {
      log_w("Konnte %s nicht löschen", filename);
    }
  }
  xSemaphoreGive(semFile);
  return count;
}

int deleteFiles(const char *filename)
{
  if (strcmp(filename, "*") == 0)
    return deleteAllFiles(FILE_SUFFIX);

  return deleteFile(filename);
}

// Sucht die älteste(newest = false) oder neueste (true) Datei. Achtung gibt evtl currentFile zurück.
String findFile(const bool newest, const char *fileext)
{
  String bestName;
  bestName.reserve(32);

  time_t bestTime = newest ? LONG_MIN : LONG_MAX;

  xSemaphoreTake(semFile, portMAX_DELAY);

  File root = LittleFS.open("/");
  File fnext = root.openNextFile();

  while (fnext)
  {
    if (endsWith(fnext.name(), fileext))
    {
      time_t written = fnext.getLastWrite();
      if ((newest && written > bestTime) || (!newest && written < bestTime))
      {
        bestTime = written;
        bestName = '/' + String(fnext.name());
      }
    }
    fnext = root.openNextFile();
  }

  root.close();
  xSemaphoreGive(semFile);

  return bestName;
}
