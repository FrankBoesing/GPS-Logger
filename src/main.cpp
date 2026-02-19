#include "includes.h"
#include <WiFiMulti.h>
#include <TinyGPSPlus.h>
#include "ota.h"

WiFiMulti wifiMulti;
std::atomic<log_mode_t> logMode = NOLOG;
std::atomic<bool> logAppend = 1;
std::atomic<log_cmd_t> logCmd = NOPE;

static size_t _fsTotalBytes;
const size_t &fsTotalBytes = _fsTotalBytes; // make it read-only
ulong firstFix = 0;
gps_state_ctx_t gps_state = {};

/****************************************************************************************************************************/
/****************************************************************************************************************************/
/****************************************************************************************************************************/

static void saveToGPSLog(TinyGPSPlus &gps, const time_t &utc);

static void timeFromGPS(TinyGPSPlus &gps, time_t &utc)
{
	if (!gps.time.isValid() || !gps.date.isValid() || gps.date.month() < 1)
		return;

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
		return;

	if (lastSystemTimeSync == 0 || t - lastSystemTimeSync > 15 * 60) // Sync alle 15 Minuten
	{
		tv.tv_sec = t;
		tv.tv_usec = gps.time.centisecond() * 10000; // 1/100 s → µs
		settimeofday(&tv, NULL);
		lastSystemTimeSync = t;
		logd("Systemzeit von GPS gesetzt.");
	}

	utc = t;
	return;
}

static void handleGPSData()
{
	static TinyGPSPlus gps;
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

		if (gps.encode((char) ch))
		{
			digitalWrite(LED, HIGH);

			time_t utc = 0;
			timeFromGPS(gps, utc);
			saveToGPSLog(gps, utc);

			digitalWrite(LED, LOW);
			return;
		}
	}
}

/****************************************************************************************************************************/
/****************************************************************************************************************************/

static void startStop(const time_t &utc)
{
	static bool booted = true;
	const log_cmd_t cmd = logCmd;

	if (cmd == STOPNOW)
	{ // Loggen durch die UI abgeschaltet
		logfile.close();
	}
	else if (!logfile)
	{
		if (cmd == STARTNOW ||					 // Startkommando (ui Button)
			(logMode == LOGAUTOSTART && booted)) // Start nach Boot (1. Fix)
		{
			logd("Start log");
			booted = false;
			logfile.open(utc);
			yield();
		}
	}

	if (cmd)
		logCmd = NOPE;
}

/****************************************************************************************************************************/
#define TIME_GPS_HANDLING false
static void saveToGPSLog(TinyGPSPlus &gps, const time_t &utc) // Wird sekündlich aufgerufen
{
	static time_t prevUtc = 0;

	if (prevUtc == utc)
		return;

	const ulong m = micros();
	startStop(utc);

	float dt_gps = 1.0f;
	if (prevUtc != 0 && utc > prevUtc)
	{
		dt_gps = (float)(utc - prevUtc);
	}
	prevUtc = utc;

	const uint8_t fix = gps.location.FixQuality() - '0';
	if (firstFix == 0 && fix > 0)
		firstFix = m;

	bool ok = gps_state_update((gps_data_t){
								   .lat = gps.location.lat(),
								   .lng = gps.location.lng(),
								   .hdop = (float)gps.hdop.hdop(),
								   .kmh = (float)gps.speed.kmph(),
								   .course = (float)gps.course.deg(),
								   .dt_gps = dt_gps,
								   .satellites = (uint8_t)gps.satellites.value(),
								   .fix = fix},
							   gps_state);

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
	logi("GPS handling: %u ms", (micros() - m) / 1000);
#endif
}

/****************************************************************************************************************************/
/****************************************************************************************************************************/

#pragma GCC push_options
#pragma GCC optimize("Os")

/****************************************************************************************************************************/
static void wifiMulti_run()
{
	if (WiFi.status() != WL_CONNECTED && WiFi.softAPgetStationNum() == 0)
	{
		static unsigned long lastScan = 0;
		if (millis() - lastScan > 10000)
		{ // Nur alle 10 Sek.
			wifiMulti.run();
			lastScan = millis();
		}
	}
}
/****************************************************************************************************************************/

static bool error(const char *msg = nullptr)
{
	static char err[32] = {};
	static ulong t = 0;

	if (msg)
		strlcpy(err, msg, sizeof(err));

	if (err[0]) {

		const ulong m = millis();
		if (m - t > 80)
		{
			t = m;
			digitalWrite(LED, !digitalRead(LED) );
			loge("%s", err);
		}
		return true;
	}

	return false;
}

void setup()
{
	pinMode(LED, OUTPUT);
	digitalWrite(LED, HIGH);

	WiFi.useStaticBuffers(true);

	Serial.begin(115200);
	initRamLogging();

	GPSSerial.setRxBufferSize(512);
	GPSSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
	while (GPSSerial.read() >= 0)
		;
	GPSSerial.setTimeout(5000);

	wifiCreds.resize(WIFI_MAX_NETWORKS);
	if (!LittleFS.begin(false))
		error("LittleFS Fehler!");

	if (!LittleFS.exists("/web/index.html"))
		error("LittleFS: WEBUI nicht vorhanden");

	_fsTotalBytes = LittleFS.totalBytes();

	WiFi.onEvent(onWiFiEvent);
	WiFi.setHostname(HOSTNAME); // muss die erste Einstellung sein
	WiFi.mode(WIFI_AP_STA);
	esp_wifi_set_max_tx_power((int8_t)((float)WiFI_MAX_POWER * 4.0f));

	WiFi.softAP(AP_SSID, AP_PASS, 6);
	delay(100); // Funktioniert besser mit delay.
	loadPrefs();

	if (wifiCreds.size())
	{
		for (const auto &e : wifiCreds)
			wifiMulti.addAP(e.ssid, e.pass);
		wifiMulti.run();
	}
	WiFi.setAutoReconnect(true);

	initLogfile();
	setupWebServer();

	if (logMode == LOGAUTOSTART)
		logCmd = STARTNOW;

	cleanupStorage();
	readFileList(FILE_SUFFIX);

	initOTA();
	initTelnetLogging();
	logi("---- Access Point  ----");
	logi("SSID       : %s", AP_SSID);
	logi("AP-Passwort: %s", AP_PASS);
	logi("AP-IP      : %s", WiFi.softAPIP().toString().c_str());
	logi("-----------------------");

	if (!GPSSerial.find("\n"))
		error("GPS nicht verbunden.");
	hwinit();

	logi("Setup abgeschlossen.");

	digitalWrite(LED, LOW);
	yield();
}

/****************************************************************************************************************************/

void loop()
{
	wifiMulti_run();
	handleOTA();
	if (!error())
		{
			handleGPSData();
			boost();
		}
}

#pragma GCC pop_options
