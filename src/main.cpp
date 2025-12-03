#include <WiFi.h>
#include <ESPmDNS.h>
#include <TinyGPSPlus.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <time.h>
#include "config.h"

#include "utils.h"
#include "gps_hw.h"
#include "web.h"
#include "credentials.h"

/*
  TODO: Status im Webinterface erweitern. Flash Belegung. Build-Datum. Die-Temperatur: temperatureRead()

  IDEEN für zukünftige Erweiterungen:
  - Plugin-System für optionale Erfassung von - Höhe - Geschwindigkeit - Bordspannung - Temperatur.. etc
     Im Dateiheader müsste dafür ein Wert für die Konfiguration der Aufnahme hinterlegt sein.
*/
volatile eLogMode logMode = NoLog;
volatile eLogCmd logCmd = nope;

GPSInfo gps = {0};

/****************************************************************************************************************************/
/****************************************************************************************************************************/
/****************************************************************************************************************************/

static void saveToGPSLog();

static bool setGPSInfo(TinyGPSPlus &tinygps)
{
  static uint8_t secs = tinygps.time.second();
  const uint8_t gsecond = tinygps.time.second();
  if (gsecond == secs)
    return false; // Update once a second only
  secs = gsecond;

  gps.age = (int)tinygps.location.age();
  gps.satellites = tinygps.satellites.isValid() ? (int)tinygps.satellites.value() : 0;
  gps.sentences = (int)tinygps.sentencesWithFix();
  gps.speed = tinygps.speed.isValid() ? (float)tinygps.speed.kmph() : -1.0f;
  gps.quality = (char)tinygps.location.FixQuality() - '0';
  gps.valid = tinygps.location.isValid() && gps.quality > 0 && gps.satellites >= GPS_MIN_SATELLITES;

  const uint8_t gmonth = tinygps.date.month();
  if (gmonth > 0)
  {
    struct tm tt = {
        .tm_sec = gsecond,
        .tm_min = tinygps.time.minute(),
        .tm_hour = tinygps.time.hour(),
        .tm_mday = tinygps.date.day(),
        .tm_mon = gmonth - 1,
        .tm_year = tinygps.date.year() - 1900,
        .tm_isdst = 0};

    gps.gpstime = mktime(&tt);
  }
  else
    gps.gpstime = 0;

  if (gps.valid)
  {
    gps.point = {
        .lat = (gpsfloat_t)tinygps.location.lat(),
        .lon = (gpsfloat_t)tinygps.location.lng(),
        .time = (time32_t)(gps.gpstime - TIMEOFFSET)};
  }
  else
    gps.point = {0};
  return true;
}

static void handleGPSData()
{
  static TinyGPSPlus tinygps;
  int ch;
  bool newData = false;

  while ((ch = GPSSerial.read()) >= 0)
  {
    if (CORE_DEBUG_LEVEL >= ARDUHAL_LOG_LEVEL_VERBOSE && tinygps.location.FixQuality() < '1')
    {
      static char buf[128];
      static size_t buflen = 0;
      if (ch == '\n' && buflen > 0)
      {
        buf[buflen] = 0;
        buflen = 0;
        log_v(buf);
      }
      else if (buflen < sizeof(buf) - 1)
        buf[buflen++] = (char)ch;
    }

    if (tinygps.encode((char)ch))
    {
      newData = true;
      break;
    }
  }

  if (newData)
  { // gets called with every GPS Sentence
    LEDON();
    if (setGPSInfo(tinygps))
      saveToGPSLog();
    LEDOFF();
  }
}

/****************************************************************************************************************************/
/****************************************************************************************************************************/

// Uhrzeit von GPS setzen
static void syncTimeFromGPS()
{
  // wird eigentlich nicht benötigt. evtl zum debuggen sinnvoll.
  if (0)
    return;

  static bool synced = false;

  // sync nur, wenn noch keines stattgefunden hat
  if (synced)
    return;

  struct timeval tv = {.tv_sec = gps.gpstime};
  settimeofday(&tv, NULL);

  synced = true;
  log_i("Zeit von GPS gesetzt");
  return;
}

/****************************************************************************************************************************/

static void saveToGPSLog() // called once a second
{

  // Möglicherweise wurde durch die Web-ui das Loggen abgeschaltet:
  if (logCmd == stopNow)
  {
    logfile.close();
    logCmd = nope;
  }

  if (gps.gpstime > 0)
    syncTimeFromGPS();

  logGPSInfo(gps);

  if (!gps.valid)
    return;

  if (!logfile)
  {
    if (logCmd == startNow || (logMode == LogAfterMinSpeed && gps.speed >= (float)MIN_SPEED_TO_START))
    {
      logfile.open(gps.gpstime);
      logCmd = nope;
    }
  }

  if (logfile)
  {
    logfile.writePoint(gps.point);
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
/****************************************************************************************************************************/
/****************************************************************************************************************************/

void setup()
{
  Serial.begin(SERIAL_BAUD);

  pinMode(LED, OUTPUT);
  LEDON();

  if (!LittleFS.begin(false))
    error("LittleFS Fehler!");

  if (!LittleFS.exists("/web/index.html"))
    error("LittleFS: WEBUI nicht vorhanden");

  initLogfile();

  WiFi.setHostname(HOSTNAME); // muss die erste Einstellung sein
  WiFi.useStaticBuffers(true);
  WiFi.mode(WIFI_AP_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  esp_wifi_set_max_tx_power((int8_t)((float)WiFI_MAX_POWER * 4.0f));
  esp_wifi_set_ps(WiFi_POWER_MODE);
  WiFi.softAP(AP_SSID, AP_PASS);
  MDNS.begin(HOSTNAME);
  MDNS.addService("http", "tcp", 80);
  setupWebServer();

  loadPrefs();

  GPSSerial.setRxBufferSize(1024);
  GPSSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

  if (!isGPSConnected())
    error("GPS nicht verbunden!");

  hwinit();

  if (logMode == LogAfterBoot)
    logCmd = startNow;

  setCpuFrequencyMhz(CPU_FREQ_SLOW);
  log_i("Setup abgeschlossen.");
  LEDOFF();
}

/****************************************************************************************************************************/

void loop()
{
  handleGPSData();
  vTaskDelay(pdMS_TO_TICKS(1));
}
