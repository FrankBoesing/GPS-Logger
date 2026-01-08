#include "includes.h"

#include <ArduinoJson.h>
#include <Preferences.h>

static AsyncWebServer server(80);
static AsyncEventSource events("/events");
static SemaphoreHandle_t semDL; // Download
static char JsonBuf[4096];

enum DLState
{
	Done,
	Header,
	Points,
	Footer
};
struct DContext
{
	logfileR f;
	DLState state;
	DContext() : state(Done) {}
	bool begin(const char *path)
	{
		bool r = f.open(path);
		state = r ? Header : Done;
		return r;
	}
	void close()
	{
		f.close();
		state = Done;
	}
};
static DContext dlCtx;

/****************************************************************************************************************************/
/****************************************************************************************************************************/

void uiSendJson(const bool firstRequest)
{
	static JsonDocument doc;
	static uint sseId = 0;
	if (events.count() == 0)
		return;

	boost(true);
	doc.clear();

	if (firstRequest)
	{
		doc["total"] = fsTotalBytes;
		doc["build"] = LAST_BUILD_TIME;
		JsonArray credArr = doc["wifi"].to<JsonArray>();
		for (int i = 0; i < WIFI_MAX_NETWORKS; ++i)
		{
			credArr.add((const char*)wifiCreds[i].ssid);
		}
	}
	doc["used"] = LittleFS.usedBytes();
	doc["active"] = (int)logfile;
	// doc["count"] = filelist.size();
	doc["q"] = lround(gps_state.accuracy_m);
	doc["firstFix"] = firstFix;
	doc["logMode"] = (int)logMode;
	doc["logAppend"] = (int)logAppend;
	doc["temp"] = temperatureRead();

	JsonArray arr = doc["files"].to<JsonArray>();
	for (const auto &e : filelist.descending())
	{
		JsonArray row = arr.add<JsonArray>();
		row.add(e.id);
		row.add(e.lastWrite);
		row.add((int)e.active);
	}

	doc["RAMminFree"] = ESP.getMinFreeHeap();
	auto written = serializeJson(doc, JsonBuf, sizeof(JsonBuf));
	if (written >= sizeof(JsonBuf) - 1)
		log_w("Json Buffer zu klein!");

	events.send(JsonBuf, "message", ++sseId);
	yield();
}
/****************************************************************************************************************************/
static constexpr int http_OK = 200;
static constexpr int http_NOCONTENT = 204;
static constexpr int http_BADREQUEST = 400;
static constexpr int http_NOTFOUND = 404;
static constexpr int http_CONFLICT = 409;
static constexpr int http_ERROR = 500;
static constexpr int http_BUSY = 503;

static bool isBadRequest(AsyncWebServerRequest *request, const char *arg)
{
	if (!request->hasParam(arg))
	{
		request->send(http_BADREQUEST);
		return true;
	}
	return false;
}

/****************************************************************************************************************************/
void setupWebServer()
{
	semDL = xSemaphoreCreateBinary();
	xSemaphoreGive(semDL);

	DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
	DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
	DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");

	server.addMiddleware([](AsyncWebServerRequest *request, auto next)
						 {
							 boost(true);
							 next(); // Lässt den Request zum eigentlichen Ziel weiterwandern
						 });

	server.on("/events", HTTP_OPTIONS, [](AsyncWebServerRequest *request)
			  { request->send(http_NOCONTENT); });

	server.on("/", HTTP_OPTIONS, [](AsyncWebServerRequest *request)
			  { request->send(http_NOCONTENT); });

	/**********************/

	server.on("/set", HTTP_POST, [](AsyncWebServerRequest *request)
			  {
				bool ok = false, prefs = false;

				if (request->hasParam("mode"))
				{
					auto mode = (log_mode_t)request->getParam("mode")->value().toInt();
					if (mode >= NOLOG && mode <= LOGAUTOSTART && logMode != mode)
					{
						logMode = mode;
						ok = prefs = true;
					}
				}

				if (request->hasParam("append"))
				{
					auto append = request->getParam("append")->value().toInt();
					if (append >= 0 && append <= 1 && append != logAppend)
					{
						logAppend = append;
						ok = prefs = true;
					}
				}

				if (request->hasParam("active"))
				{
					auto cmdNeu = (log_cmd_t)request->getParam("active")->value().toInt();
					if (logCmd == NOPE && cmdNeu >= STOPNOW && cmdNeu <= STARTNOW)
					{
						logCmd = cmdNeu; // Cmd wird nun im Haupttask ausgeführt.
						ok = true;
					}
				}

				// WiFi-Einstellungen
				for (int i = 0; i <= WIFI_MAX_NETWORKS; ++i) {
					char ssid[16];
					char pass[16];
					snprintf(ssid,  sizeof(ssid), "wifi%d", i);
					snprintf(pass,  sizeof(pass), "pass%d", i);
					if (request->hasParam(ssid) && request->hasParam(pass)) {
						if (request->getParam(ssid)->value().length() > 0 && request->getParam(pass)->value().length() > 0) {
							strlcpy(wifiCreds[i].ssid, request->getParam(ssid)->value().c_str(), sizeof(wifiCreds[i].ssid));
							strlcpy(wifiCreds[i].pass, request->getParam(pass)->value().c_str(), sizeof(wifiCreds[i].pass));
						} else {
							wifiCreds[i].ssid[0] = wifiCreds[i].pass[0] = '\0';
						}
						ok = prefs = true;
					}
				}

				request->send(ok ? http_OK : http_BADREQUEST);
				if (ok && prefs)
					savePrefs(); });
	/**********************/
	server.on("/delete", HTTP_OPTIONS, [](AsyncWebServerRequest *request)
			  { request->send(http_NOCONTENT); });
	server.on("/delete", HTTP_DELETE, [](AsyncWebServerRequest *request)
			  {
				if (isBadRequest(request, "file")) return;
				size_t cnt;
				const auto p = atoll(request->getParam("file")->value().c_str());
				if (p == -1)
					cnt = deleteAllFiles();
				else
					cnt = deleteFile(p);
				request->send(cnt ? http_OK : http_ERROR);
				if (cnt) uiSendJson(); });

	/**********************/

	/* Chunked Download mit Konvertierung vom Binär- ins xml Format */
	server.on("/download", HTTP_GET, [](AsyncWebServerRequest *request)
			  {
				if (isBadRequest(request, "file")) return;

				char fbuf[128];
				const char *idstr = request->getParam("file")->value().c_str();
				snprintf(fbuf, sizeof(fbuf), FILE_PREFIX "%s" FILE_SUFFIX, idstr);

				if (logfile.isActive(fbuf)) {
					request->send(http_CONFLICT);
					return;
				}

				if (xSemaphoreTake(semDL, pdMS_TO_TICKS(100)) != pdTRUE) {
					request->send(http_BUSY);
					return;
				}

				if (!dlCtx.begin(fbuf)) {
					xSemaphoreGive(semDL);
					request->send(http_NOTFOUND);
					return;
				}

				if (request->hasParam("raw")) { // http://gps.local/download?file=1767392372&raw
					dlCtx.close();
					request->send(LittleFS, fbuf, "application/octet-stream", true);
					xSemaphoreGive(semDL);
					return;
				}

				auto generator = [](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
					size_t pos = 0;
					constexpr std::string_view GPXHEADER = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<gpx>\n<trk><trkseg>\n";
					constexpr std::string_view GPXFOOTER = "</trkseg></trk>\n</gpx>";

					switch (dlCtx.state) {
						case Header:
							pos = std::min(GPXHEADER.size(), maxLen);
							std::copy_n(GPXHEADER.data(), pos, buffer);
							dlCtx.state = Points;
							break;

						case Points:
							while (pos + 128 <= maxLen) {
								GPSPoint_t point;
								struct tm tm;
								if (!dlCtx.f.readPoint(point)) {
									dlCtx.state = Footer;
									break;
								}
								gmtime_r(&point.time, &tm);
								pos += snprintf((char*)buffer + pos, maxLen - pos,
									"<trkpt lat=\"%.7f\" lon=\"%.7f\"><time>%04d-%02d-%02dT%02d:%02d:%02dZ</time></trkpt>\n",
									point.lat, point.lon, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
									tm.tm_hour, tm.tm_min, tm.tm_sec);
							}
							break;

						case Footer:
							pos = std::min(GPXFOOTER.size(), maxLen);
							std::copy_n(GPXFOOTER.data(), pos, buffer);
							dlCtx.state = Done;
							break;

						case Done:
							pos = 0;
							break;
					}

					return pos;
				};

				request->onDisconnect([]() {
					dlCtx.close();
					xSemaphoreGive(semDL);
				});

				AsyncWebServerResponse *response =
					request->beginChunkedResponse("application/octet-stream", generator);
				snprintf(fbuf, sizeof(fbuf), "attachment; filename=\"%s.gpx\"", idstr);
				response->addHeader("Content-Disposition", fbuf);
				response->addHeader("X-Content-Type-Options", "nosniff");
				request->send(response); });

	/**********************/
	static constexpr const char *cachectrl = CORE_DEBUG_LEVEL >= ARDUHAL_LOG_LEVEL_DEBUG
												 ? "no-cache, no-store, must-revalidate"
												 : "max-age=86400";

	server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
			  {
		AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/web/index.html", "text/html");
		response->addHeader("Cache-Control", cachectrl);
		request->send(response); });

	server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest *request)
			  {
		AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/web/script.js", "application/javascript");
		response->addHeader("Cache-Control", cachectrl);
		request->send(response); });

	server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request)
			  {
		AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/web/style.css", "text/css");
		response->addHeader("Cache-Control", cachectrl);
		request->send(response); });

	events.onConnect([](AsyncEventSourceClient *client)
					 {
						boost(true);
						client->send("{}", "message");
						yield();
						uiSendJson(true);
						log_i("SSE client connected"); });

	events.onDisconnect([](const auto *cb)
						{ log_v("SSE Client Disconnect"); });
	server.addHandler(&events);

	server.begin();
}
