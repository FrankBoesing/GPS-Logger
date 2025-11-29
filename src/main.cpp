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

// TODO: Status im Webinterface erweitern. Flash Belegung. Build-Datum.
// IDEE: WLAN abschalten wenn in Bewegung? Nach x Minuten?  WiFi.disconnect(true); WiFi.mode(WIFI_OFF); -> Km/h Anzeige nicht möglich.

volatile eLogMode logMode = NoLog;
volatile eLogCmd logCmd = nope;
char gpsFixQuality = '0';

TinyGPSPlus gps;

/****************************************************************************************************************************/
/****************************************************************************************************************************/
/****************************************************************************************************************************/

static struct tm gpstime()
{
  return {
      .tm_sec = gps.time.second(),
      .tm_min = gps.time.minute(),
      .tm_hour = gps.time.hour(),
      .tm_mday = gps.date.day(),
      .tm_mon = gps.date.month() - 1,
      .tm_year = gps.date.year() - 1900,
      .tm_isdst = 0};
}

static GPSPoint setGPSPoint()
{
  struct tm t = gpstime();
  time_t utc = timegm(&t);

  GPSPoint p = {
      .lat = (gpsfloat_t)gps.location.lat(),
      .lon = (gpsfloat_t)gps.location.lng(),
      .time = (time32_t)(utc - TIMEOFFSET)};

  return p;
}

// Uhrzeit von GPS setzen
static bool syncTimeFromGPS(const unsigned long loopmillis)
{
  static unsigned long lastGPSTimeSync = 0;

#if GPS_TIMESYNC_ONCE
  // sync nur, wenn noch keines stattgefunden hat
  if (lastGPSTimeSync > 0)
    return true;
  if (gps.date.month() == 0)
    return false;
#else
  if (gps.date.month() == 0)
    return false;
  if ((lastGPSTimeSync > 0) && (loopmillis - lastGPSTimeSync < GPS_TIMESYNC_INTERVAL_MS))
    return true;
#endif

  lastGPSTimeSync = loopmillis;
  struct tm tt = gpstime();
  struct timeval tv = {.tv_sec = timegm(&tt)};

  settimeofday(&tv, NULL);
  log_i("Zeit von GPS gesetzt");
  return true;
}

static void saveToGPSLog(const unsigned long loopmillis)
{
  static unsigned long timeLastPoint = 0;

  // Möglicherweise wurde durch die Web-ui das Loggen abgeschaltet:
  if (logCmd == stopNow)
  {
    logfile.close();
    logCmd = nope;
    return;
  }

  if (!syncTimeFromGPS(loopmillis))
    return; // GPS Zeit wurde noch nicht gesetzt

  if (logCmd == startNow && !logfile)
  {
    logfile.open();
    logCmd = nope;
    return;
  }

  if (!gps.location.isUpdated())
    return;

  if (loopmillis - timeLastPoint < 950UL)
    return;

  gpsFixQuality = (char)gps.location.FixQuality();

  if (!(gps.location.isValid() &&
        (gpsFixQuality > gps.location.Invalid)))
    return;

  timeLastPoint = loopmillis;

  if (!logfile && logMode == LogAfterMinSpeed && gps.speed.isValid() && (float)gps.speed.kmph() >= (float)MIN_SPEED_TO_START)
  {
    logfile.open();
    logCmd = nope;
    return;
  }

  if (logfile)
  {
    GPSPoint p = setGPSPoint();
    logfile.writePoint(&p);
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
    if (!handleHwData(ch))
    {
      if (gps.encode(ch))
        newData = true;
      printSerialData(ch);
    }
  }

  if (newData)
  { // gets called with every GPS Sentence
    LEDON();
    unsigned long loopmillis = millis();
    saveToGPSLog(loopmillis);
    logGPSInfo(loopmillis);
    LEDOFF();
  }
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
  initGPSfile();

  if (!LittleFS.exists("/web/index.html"))
    error("LittleFS: WEBUI nicht vorhanden");

  setenv("TZ", TIMEZONE, 1);
  tzset();
  initTimeOffset();

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

  GPSSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

  if (!isGPSConnected())
    error("GPS nicht verbunden!");

  hwinit();

  if (logMode == LogAfterBoot)
    logCmd = startNow;
  log_i("Setup abgeschlossen.");
  LEDOFF();
}

/****************************************************************************************************************************/

void loop()
{
  handleGPSData();
  vTaskDelay(pdMS_TO_TICKS(1));
}
