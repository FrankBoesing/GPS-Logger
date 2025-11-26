#include <WiFi.h>
#include <ESPmDNS.h>
#include <TinyGPSPlus.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <time.h>
#include "config.h"
#include "utils.h"
#include "debug.h"
#include "gps_hw.h"
#include "web.h"
#include "credentials.h"

// TODO: Status im Webinterface erweitern. Flash Belegung. Build-Datum.
// IDEE: WLAN abschalten wenn in Bewegung? Nach x Minuten?  WiFi.disconnect(true); WiFi.mode(WIFI_OFF); -> Km/h Anzeige nicht möglich.
// IDEE: Datenkomprimierung(?) -> keine Anzeige der Dateigröße möglich... statdessen prozentual bzw kb?

File currentFile;
SemaphoreHandle_t semFile;
volatile eLogMode logMode = NoLog;
volatile eLogCmd logCmd = nope;
char gpsFixQuality = '0'; // Invalid = '0', GPS = '1', DGPS = '2', PPS = '3', RTK = '4', FloatRTK = '5', Estimated = '6', Manual = '7', Simulated = '8'

TinyGPSPlus gps;
unsigned long lastGPSTimeSync = 0;

static size_t _fsTotalBytes;
const size_t &fsTotalBytes = _fsTotalBytes; // make it read-only

/****************************************************************************************************************************/
/****************************************************************************************************************************/
/****************************************************************************************************************************/

static size_t pointsInFileCache = 0;

static void closeLogFile()
{
  if (currentFile)
  {
    xSemaphoreTake(semFile, portMAX_DELAY);
    currentFile.flush();
    currentFile.close();
    xSemaphoreGive(semFile);
    log_i("Datei geschlossen");
  }

  pointsInFileCache = 0;
}

static void openLogFile()
{
  if (currentFile)
    return;

  // Falls in LogMode eingeschaltet, prüfen ob an die jüngste Datei angehängt werden soll:
  if (RESTART_AFTER_IDLE) // An letztes Logfile anfügen?
  {
    time_t now;
    time(&now);

    String fname = findFile(true);
    xSemaphoreTake(semFile, portMAX_DELAY);
    File f = LittleFS.open(fname, FILE_READ);
    time_t lastWritten = f.getLastWrite();
    f.close();

    if (now - lastWritten <= MAX_IDLE_SECONDS)
    {
      currentFile = LittleFS.open(fname, FILE_APPEND);
      log_i("Füge an Logfile an: %s Alter: %us", fname.c_str(), now - lastWritten);
    }
    xSemaphoreGive(semFile);
  }

  if (!currentFile)
  {
    char filename[32];
    time_t now;
    time(&now);

    // use time as filename:
    snprintf(filename, sizeof(filename), FILE_PREFIX "%lld" FILE_SUFFIX, now);

    xSemaphoreTake(semFile, portMAX_DELAY);
    currentFile = LittleFS.open(filename, FILE_WRITE);
    log_i("Neues Logfile: %s", filename);
    xSemaphoreGive(semFile);
  }

  if (currentFile)
  {
    xSemaphoreTake(semFile, portMAX_DELAY);
    currentFile.setBufferSize(sizeof(GPSPoint) * FILECACHE_MAXPOINTS); // Sichergehen, dass die Punkte in den File-Cache passen
    xSemaphoreGive(semFile);
  }
  pointsInFileCache = 0;
}

static void logPoint(GPSPoint *p)
{
  if (currentFile)
  {
    xSemaphoreTake(semFile, portMAX_DELAY);
    currentFile.write((const uint8_t *)p, sizeof(GPSPoint));
    pointsInFileCache++;
    if (pointsInFileCache >= FILECACHE_MAXPOINTS)
    {
      currentFile.flush();
      //      vTaskDelay(100 / portTICK_PERIOD_MS);
      pointsInFileCache = 0;
      log_d("Cache flushed");
    }
    xSemaphoreGive(semFile);
  }
  else
    log_e("Kann nicht loggen. Logfile geschlossen");
}

/****************************************************************************************************************************/
/****************************************************************************************************************************/

static GPSPoint setGPSPoint()
{
  time_t utc;
  struct tm t = {
      .tm_sec = gps.time.second(),
      .tm_min = gps.time.minute(),
      .tm_hour = gps.time.hour(),
      .tm_mday = gps.date.day(),
      .tm_mon = gps.date.month() - 1,
      .tm_year = gps.date.year() - 1900,
      .tm_isdst = 0};

  utc = timegm(&t);

  GPSPoint p = {
      .lat = (float)gps.location.lat(),
      .lon = (float)gps.location.lng(),
      .time = (time32_t)(utc - TIMEOFFSET)};

  return p;
}

// Uhrzeit von GPS setzen
static void syncTimeFromGPS()
{
  unsigned long t = millis();
  if (lastGPSTimeSync && (t - lastGPSTimeSync < GPS_TIMESYNC_INTERVAL_MS))
    return;

  if (gps.date.month() == 0)
    return;

  lastGPSTimeSync = t;

  struct tm tt = {
      .tm_sec = gps.time.second(),
      .tm_min = gps.time.minute(),
      .tm_hour = gps.time.hour(),
      .tm_mday = gps.date.day(),
      .tm_mon = gps.date.month() - 1,
      .tm_year = gps.date.year() - 1900,
      .tm_isdst = 0};

  struct timeval tv = {
      .tv_sec = timegm(&tt)};

  settimeofday(&tv, NULL);
  log_i("Zeit von GPS gesetzt");
}

static void saveToGPSLog()
{
  static unsigned long timeLastPoint = 0;
  unsigned long t;
  float speed;

  // Möglicherweise wurde durch die Web-ui das Loggen abgeschaltet:
  if (logCmd == stopNow)
  {
    closeLogFile();
    logCmd = nope;
    return;
  }

  if (!lastGPSTimeSync)
    return; // GPS Zeit wurde noch nicht gesetzt

  if (!currentFile && logCmd == startNow)
  {
    openLogFile();
    logCmd = nope;
    return;
  }

  if (!gps.location.isUpdated())
    return;

  t = millis();
  if (t - timeLastPoint < 950UL * GPS_LOGINTERVAL)
    return;

  gpsFixQuality = (char)gps.location.FixQuality();

  if (!(gps.location.isValid() &&
        (gpsFixQuality > gps.location.Invalid)))
    return;

  timeLastPoint = t;

  speed = gps.speed.isValid() ? gps.speed.kmph() : 0.0f;

  if (!currentFile && (logMode == LogAfterBoot ||
                       (logMode == LogAfterMinSpeed && gps.speed.isValid() && speed >= MIN_SPEED_TO_START)))
  {
    openLogFile();
    logCmd = nope;
    return;
  }

  if (currentFile)
  {
    GPSPoint p = setGPSPoint();
    logPoint(&p);
  }
}

/****************************************************************************************************************************/
/****************************************************************************************************************************/
static void loadPrefs()
{
  Preferences preferences;
  preferences.begin("gps", true);
  logMode = (eLogMode)preferences.getChar("logMode", (char)DEFAULTLOGMODE);
  preferences.end();
}

void savePrefs()
{
  Preferences preferences;
  preferences.begin("gps", false);
  preferences.putChar("logMode", (char)logMode);
  preferences.end();
}

/****************************************************************************************************************************/

static void handleGPSData()
{

  int ch;
  bool newData = false;

  while ((ch = GPSSerial.read()) >= 0)
  {
    LEDON();
    if (!handleHwData(ch))
    {
      newData = gps.encode(ch);
      printSerialData(ch);
      if (newData)
        break;
    }
  }

  if (newData)
  {
    syncTimeFromGPS();
    saveToGPSLog();
    logGPSInfo();
  }
  LEDOFF();
}

/****************************************************************************************************************************/
/****************************************************************************************************************************/
/****************************************************************************************************************************/

void setup()
{
  Serial.begin(SERIAL_BAUD);

  pinMode(LED, OUTPUT);
  LEDON();

  loadPrefs();

  semFile = xSemaphoreCreateBinary();
  xSemaphoreGive(semFile);

  if (!LittleFS.begin(false))
    error("LittleFS Fehler!");

  if (!LittleFS.exists("/web/index.html"))
    error("LittleFS: WEBUI nicht vorhanden");

  _fsTotalBytes = LittleFS.totalBytes();

  setenv("TZ", TIMEZONE, 1);
  tzset();
  initTimeOffset();

  WiFi.setHostname(HOSTNAME); // muss die erste Einstellung sein
  WiFi.useStaticBuffers(true);
  WiFi.mode(WIFI_AP_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  esp_wifi_set_max_tx_power(WiFI_MAX_POWER * 4);
  esp_wifi_set_ps(WiFi_POWER_MODE);
  WiFi.softAP(AP_SSID, AP_PASS);
  MDNS.begin(HOSTNAME);
  MDNS.addService("http", "tcp", 80);

  setupWebServer();

  GPSSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

  if (!isGPSConnected())
    error("GPS nicht verbunden!");

  hwinit();

  log_i("Setup abgeschlossen.");
  LEDOFF();
}

/****************************************************************************************************************************/

void loop()
{
  handleGPSData();
}
