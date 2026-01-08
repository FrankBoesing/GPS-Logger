#include "includes.h"
#include <WiFiMulti.h>
#include <TinyGPSPlus.h>

/*
	IDEEN für zukünftige Erweiterungen:
	- Plugin-System für optionale Erfassung von - Höhe - Geschwindigkeit - Bordspannung - Temperatur.. etc
		 Im Dateiheader müsste dafür ein Wert für die Konfiguration der Aufnahme hinterlegt sein.
*/

WiFiMulti wifiMulti;
std::atomic<log_mode_t> logMode = NOLOG;
std::atomic<bool> logAppend = 1;
std::atomic<log_cmd_t> logCmd = NOPE;

time_t utc = 0;
ulong firstFix = 0;
gps_state_ctx_t gps_state = {};

/****************************************************************************************************************************/
/****************************************************************************************************************************/
/****************************************************************************************************************************/

static void saveToGPSLog(TinyGPSPlus &gps);

static time_t timeFromGPS(TinyGPSPlus &gps)
{
	if (!gps.time.isValid() || !gps.date.isValid() || gps.date.month() < 1)
		return 0;

	static time_t lastSystemTimeSync = 0;
	struct timeval tv;
	struct tm tm = {0};

	tm.tm_year = gps.date.year() - 1900; // Jahre seit 1900
	tm.tm_mon = gps.date.month() - 1;	 // 0–11
	tm.tm_mday = gps.date.day();

	tm.tm_hour = gps.time.hour();
	tm.tm_min = gps.time.minute();
	tm.tm_sec = gps.time.second();

	const time_t t = mktime(&tm);
	if (t <= 0)
		return 0;

	utc = t;

	if (t - lastSystemTimeSync > 15 * 60) // Sync alle 15 Minuten
	{
		tv.tv_sec = utc;
		tv.tv_usec = gps.time.centisecond() * 10000; // 1/100 s → µs
		settimeofday(&tv, NULL);
		lastSystemTimeSync = t;
		log_d("Systemzeit von GPS gesetzt.");
	}
	return t;
}

static void handleGPSData()
{
	static TinyGPSPlus gps;
	static time_t lastUtc;
	int ch;

	while ((ch = GPSSerial.read()) >= 0)
	{
		// Rohdaten ausgeben:
		if (false && CORE_DEBUG_LEVEL >= ARDUHAL_LOG_LEVEL_VERBOSE)
		{
			static char buf[128];
			static size_t buflen = 0;
			if (ch == '\n' && buflen > 0)
			{
				buf[buflen] = 0;
				buflen = 0;
				Serial.println(buf);
			}
			else if (buflen < sizeof(buf) - 1)
				buf[buflen++] = (char)ch;
		}

		if (gps.encode((char)ch))
		{
			LEDON();

			utc = timeFromGPS(gps);
			if (utc > lastUtc)
			{
				lastUtc = utc;
				saveToGPSLog(gps);
			}

			LEDOFF();
			return;
		}
	}
}

/****************************************************************************************************************************/
/****************************************************************************************************************************/
static void startStop()
{
	static bool booted = true;
	const log_cmd_t cmd = logCmd;

	if (cmd == STOPNOW)
	{ // Loggen durch die UI abgeschaltet
		logfile.close();
	}
	else if (!logfile && utc > 0)
	{
		if (cmd == STARTNOW ||					 // Startkommando (ui Button)
			(logMode == LOGAUTOSTART && booted)) // Start nach Boot (1. Fix)
		{
			log_d("startlog");
			booted = false;
			logfile.open(utc);
		}
	}

	if (cmd)
		logCmd = NOPE;
}

/****************************************************************************************************************************/
#define TIME_GPS_HANDLING false
static void saveToGPSLog(TinyGPSPlus &gps) // Wird sekündlich aufgerufen
{
	static time_t prevUtc = 0;
	const ulong m = micros();

	const uint fix = gps.location.FixQuality() - '0';
	if (firstFix == 0 && fix > 0)
		firstFix = m;

	float dt_gps = 1.0f;
	if (prevUtc != 0 && utc > prevUtc)
	{
		dt_gps = (float)(utc - prevUtc);
	}
	prevUtc = utc;

	const bool ok = gps_state_update(gps_state,
									 fix,
									 gps.satellites.value(),
									 (float)gps.hdop.hdop(),
									 (float)gps.speed.kmph(),
									 (float)gps.course.deg(),
									 gps.location.lat(),
									 gps.location.lng(),
									 dt_gps);
	if (logfile)
	{
		if (ok)
		{
			bool flush = gps_state.mayFlush || logfile.getPoints() == 0;

			if (flush || gps_state.motion_state == GPS_MOVING)
				logfile.writePoint(gps.location.lat(), gps.location.lng(), utc, flush);
		}
		logfile.periodicFlush();
	}

#if TIME_GPS_HANDLING
	log_i("GPS handling: %u ms", (micros() - m) / 1000);
#endif
}

/****************************************************************************************************************************/
/****************************************************************************************************************************/
/****************************************************************************************************************************/


/****************************************************************************************************************************/

void setup()
{
	pinMode(LED, OUTPUT);
	LEDON();

	WiFi.useStaticBuffers(true);

	Serial.begin();
	GPSSerial.setRxBufferSize(512);
	GPSSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
	while (GPSSerial.read() >= 0)
		;
	GPSSerial.setTimeout(5000);

	if (!LittleFS.begin(false))
		error("LittleFS Fehler!");

	if (!LittleFS.exists("/web/index.html"))
		error("LittleFS: WEBUI nicht vorhanden");

	WiFi.onEvent(onWiFiEvent);
	WiFi.setHostname(HOSTNAME); // muss die erste Einstellung sein
	WiFi.mode(WIFI_AP_STA);
	esp_wifi_set_max_tx_power((int8_t)((float)WiFI_MAX_POWER * 4.0f));

	WiFi.softAP(AP_SSID, AP_PASS, 1);
	delay(100); // Funktioniert besser mit delay.
	loadPrefs();

	WiFi.setAutoReconnect(true);

	for (int i = 0; i < WIFI_MAX_NETWORKS; ++i) {
		if (strlen(wifiCreds[i].ssid) == 0) continue;
  		wifiMulti.addAP(wifiCreds[i].ssid, wifiCreds[i].pass);
	}
	wifiMulti.run();
	delay(100);

	setupWebServer();

	initLogfile();
	if (logMode == LOGAUTOSTART)
		logCmd = STARTNOW;

	cleanupStorage();
	readFileList(FILE_SUFFIX);

	log_i("--- Access Point Informationen ---");
	log_i("SSID       : %s", AP_SSID);
	log_i("AP-Passwort: %s", AP_PASS);
	log_i("AP-IP      : %s", WiFi.softAPIP().toString().c_str());
	log_i("-----------------------\n");

	if (!GPSSerial.find("\n"))
		error("GPS nicht verbunden.");
	hwinit();

	log_i("Setup abgeschlossen.");

	LEDOFF();
}

/****************************************************************************************************************************/

void loop()
{
	startStop();
	yield();
	handleGPSData();
	boost(false);
}
