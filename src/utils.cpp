#include "utils.h"

/****************************************************************************************************************************/
/****************************************************************************************************************************/
// ESP32 hat kein timegm implementiert. Reparatur:

static long _offset_seconds = 0;
const long &offset_seconds = _offset_seconds; // make it readonly

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
bool str_to_ll(const char *str, long long *out)
{
  while (isspace((unsigned char)*str))
    str++;

  int neg = 0;
  if (*str == '+')
  {
    str++;
  }
  else if (*str == '-')
  {
    neg = 1;
    str++;
  }

  if (!isdigit((unsigned char)*str))
    return false;

  long long value = 0;
  for (; *str; str++)
  {
    if (!isdigit((unsigned char)*str))
      break;
    int digit = *str - '0';
    value = value * 10 + digit;
  }

  *out = neg ? -value : value;
  return true;
}
/****************************************************************************************************************************/
// Refresh the file list
void readFileList(JsonObject &fileList, const char *fileext)
{
  xSemaphoreTake(semFile, portMAX_DELAY);

  fileList.clear();
  JsonArray arr = fileList["files"].to<JsonArray>();

  File root = LittleFS.open("/");
  File fnext = root.openNextFile();
  while (fnext)
  {
    size_t sz = fnext.size();
    const char *name = fnext.path();
    if (sz > 0 && endsWith(name, fileext))
    {
      JsonObject obj = arr.add<JsonObject>();
      obj["path"] = name;
      obj["active"] = logfile.isActive(name);
      obj["lastpt"] = fnext.getLastWrite();
    }
    fnext.close();
    fnext = root.openNextFile();
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
    const char *name = fnext.path();
    if ((fileext == NULL || endsWith(name, fileext)) && !logfile.isActive(name))
    {
      if (LittleFS.remove(name))
      {
        count++;
        log_i("Gelöscht: %s", name);
      }
      else
      {
        log_w("Konnte %s nicht löschen", name);
      }
    }
    fnext.close();
    fnext = root.openNextFile();
  }

  root.close();
  xSemaphoreGive(semFile);
  return count;
}

static int deleteFile(const char *filename)
{
  if (!filename)
    return 0;

  int count = 0;
  xSemaphoreTake(semFile, portMAX_DELAY);
  if (LittleFS.exists(filename) && !logfile.isActive(filename))
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

// Sucht die älteste(newest = false) oder neueste (true) Datei.
bool findFile(const bool newest, char *filename, const size_t maxlen, time_t *lastWrite, const char *fileext)
{
  if (!filename || maxlen < 2 || !lastWrite)
    return false;

  bool found = false;
  time_t bestTime = newest ? (time_t)LONG_MIN : (time_t)LONG_MAX;

  xSemaphoreTake(semFile, portMAX_DELAY);
  File root = LittleFS.open("/");
  File f = root.openNextFile();
  while (f)
  {
    const char *name = f.path();
    if (fileext == NULL || endsWith(name, fileext))
    {
      time_t written = f.getLastWrite();
      if ((newest && written > bestTime) || (!newest && written < bestTime))
      {
        bestTime = written;
        strncpy(filename, name, maxlen - 1);
        filename[maxlen - 1] = '\0';
        found = true;
      }
    }
    f.close();
    f = root.openNextFile();
  }

  root.close();
  xSemaphoreGive(semFile);

  if (found)
    *lastWrite = bestTime;
  else
    filename[0] = '\0';

  return found;
}
/****************************************************************************************************************************/

// Prüfen ob GPS verbunden ist.
bool isGPSConnected()
{
  constexpr const unsigned long gpsConnectTimeout = 2000; // 2 Sekunden
  constexpr const unsigned long minNumChars = 20;

  unsigned long start = millis();

  while (GPSSerial.available() < minNumChars && millis() - start < gpsConnectTimeout)
  {
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }

  return GPSSerial.available() > 0;
}

// Fehlerbehandlung: Gibt eine Fehlermeldung aus und bleibt in einer Endlosschleife
void error(const char *msg)
{
  while (true)
  {
    log_e("Fehler: %s", msg);
    for (int i = 0; i < 5; i++)
    {
      TOGGLELED();
      delay(200);
    }
  }
}
