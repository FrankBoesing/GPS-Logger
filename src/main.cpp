#include "includes.h"
#include "ota.h"
#include <WiFiMulti.h>
#include <TinyGPSPlus.h>

WiFiMulti wifiMulti;
std::atomic<log_mode_t> logMode = NOLOG;
std::atomic<bool> logAppend = 1;
std::atomic<log_cmd_t> logCmd = NOPE;

static size_t _fsTotalBytes;
const size_t &fsTotalBytes = _fsTotalBytes; // make it read-only
uint32_t firstFix = 0;
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
			LEDON();

			time_t utc = 0;
			timeFromGPS(gps, utc);
			saveToGPSLog(gps, utc);

			LEDOFF();
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
	startStop(utc);

	float dt_gps = 1.0f;
	if (prevUtc != 0 && utc > prevUtc)
	{
		dt_gps = (float)(utc - prevUtc);
	}
	prevUtc = utc;

	const uint8_t fix = gps.location.FixQuality() - '0';
	if (fix > 0 && firstFix == 0)
		firstFix = millis();

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
		logfile.intervalFlush();
	}
}

/****************************************************************************************************************************/
/****************************************************************************************************************************/
/****************************************************************************************************************************/
static void wifiMulti_run()
{
	static uint32_t lastScan = 0;
	if (WiFi.status() != WL_CONNECTED &&
		WiFi.softAPgetStationNum() == 0 &&
		interval(lastScan, 5000) // Nur alle 5 Sek.
	)
	{
		wifiMulti.run();
	}
}
/****************************************************************************************************************************/

static void error(const char *msg = nullptr)
{
	static char err[32] = {};
	static uint32_t t = 0;

	if (msg)
		strlcpy(err, msg, sizeof(err));

	if (err[0] && interval(t, 80)) {
			LEDTOGGLE();
			loge("%s", err);
		}
}

void setup()
{
	pinMode(LED, OUTPUT);
	LEDON();

	WiFi.useStaticBuffers(true);

	Serial.begin(115200);
	initRamLogging();
	yield();

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
	delay(100);
	loadPrefs();

	if (wifiCreds.size())
	{
		for (const auto &e : wifiCreds)
			wifiMulti.addAP(e.ssid, e.pass);
		wifiMulti.run();
	}
	WiFi.setAutoReconnect(true);
	yield();

	initLogfile();
	setupWebServer();
	yield();

	cleanupStorage();
	readFileList(FILE_SUFFIX);
	yield();

	initOTA();
	initTelnetLogging();
	yield();

	logi("---- Access Point  ----");
	logi("SSID       : %s", AP_SSID);
	logi("AP-Passwort: %s", AP_PASS);
	logi("AP-IP      : %s", WiFi.softAPIP().toString().c_str());
	logi("-----------------------");

	if (!GPSSerial.find("\n"))
		error("GPS nicht verbunden.");
	hwinit();
	yield();

	if (logMode == LOGAUTOSTART)
		logCmd = STARTNOW;

	logi("Setup abgeschlossen.");

	LEDOFF();
}

/****************************************************************************************************************************/

void loop()
{
	wifiMulti_run();
	handleOTA();
	error();
	yield();
	handleGPSData();
	yield();
	boost();
}
