#include "includes.h"
#include <ArduinoJson.h>

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

void uiSendJson(const bool fileList, const bool wifiCredentials, const bool staticData)
{
	static JsonDocument doc;
	static uint sseId = 0;
	if (events.count() == 0)
		return;

	boost(true);
	doc.clear();

	doc["used"] = LittleFS.usedBytes();
	doc["active"] = (int)logfile;
	// doc["count"] = filelist.size();
	doc["q"] = lround(gps_state.accuracy_m);
	doc["firstFix"] = firstFix;
	doc["logMode"] = (int)logMode;
	doc["logAppend"] = (int)logAppend;
	doc["temp"] = temperatureRead();
	doc["RAMminFree"] = ESP.getMinFreeHeap();

	if (staticData)
	{
		doc["total"] = fsTotalBytes;
		doc["build"] = LAST_BUILD_TIME;
	}

	if (wifiCredentials)
	{
		JsonArray credArr = doc["wifi"].to<JsonArray>();
		for (const auto &e : wifiCreds)
			credArr.add((const char *)e.ssid);
		if (wifiCreds.size() < wifiCreds.capacity())
			credArr.add("");
	}

	if (fileList)
	{
		JsonArray arr = doc["files"].to<JsonArray>();
		for (const auto &e : filelist.descending())
		{
			JsonArray row = arr.add<JsonArray>();
			row.add(e.id);
			row.add(e.lastWrite);
			row.add((int)e.active);
		}
	}

	auto written = serializeJson(doc, JsonBuf, sizeof(JsonBuf));
	if (written >= sizeof(JsonBuf) - 1)
		logw("Json Buffer zu klein!");

	events.send(JsonBuf, "message", ++sseId);
	yield();
}
/****************************************************************************************************************************/
static const constexpr int http_OK = 200;
static const constexpr int http_NOCONTENT = 204;
static const constexpr int http_BADREQUEST = 400;
static const constexpr int http_NOTFOUND = 404;
static const constexpr int http_CONFLICT = 409;
static const constexpr int http_ERROR = 500;
static const constexpr int http_BUSY = 503;
static constexpr const char *cachectrl = CORE_DEBUG_LEVEL >= ARDUHAL_LOG_LEVEL_DEBUG
											 ? "no-cache, no-store, must-revalidate"
											 : "max-age=86400";

/****************************************************************************************************************************/
/* Chunked Download mit Konvertierung vom Binär- ins xml Format */
void download(AsyncWebServerRequest *request)
{
	const auto *param = request->getParam("file");
	if (!param) {
			request->send(http_BADREQUEST);
			return;
		}
	const char *idstr = param->value().c_str();
	char fbuf[128];
	snprintf(fbuf, sizeof(fbuf), FILE_PREFIX "%s" FILE_SUFFIX, idstr);

	if (logfile.isActive(fbuf))
	{
		request->send(http_CONFLICT);
		return;
	}

	if (xSemaphoreTake(semDL, pdMS_TO_TICKS(100)) != pdTRUE)
	{
		request->send(http_BUSY);
		return;
	}

	if (!dlCtx.begin(fbuf))
	{
		xSemaphoreGive(semDL);
		request->send(http_NOTFOUND);
		return;
	}

	if (request->hasParam("raw"))
	{ // http://gps.local/download?file=1767392372&raw
		dlCtx.close();
		request->send(LittleFS, fbuf, asyncsrv::T_application_octet_stream, true);
		xSemaphoreGive(semDL);
		return;
	}

	auto generator = [](uint8_t *buffer, size_t maxLen, size_t index) -> size_t
	{
		size_t pos = 0;
		const constexpr std::string_view GPXHEADER = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<gpx>\n<trk><trkseg>\n";
		const constexpr std::string_view GPXFOOTER = "</trkseg></trk>\n</gpx>";

		switch (dlCtx.state)
		{
		case Header:
			pos = std::min(GPXHEADER.size(), maxLen);
			std::copy_n(GPXHEADER.data(), pos, buffer);
			dlCtx.state = Points;
			break;

		case Points:
			while (pos + 128 <= maxLen)
			{
				GPSPoint_t point;
				struct tm tm;
				if (!dlCtx.f.readPoint(point))
				{
					dlCtx.state = Footer;
					break;
				}
				gmtime_r(&point.time, &tm);
				pos += snprintf((char *)buffer + pos, maxLen - pos,
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

	request->onDisconnect([]()
						  {
					dlCtx.close();
					xSemaphoreGive(semDL); });

	AsyncWebServerResponse *response =
		request->beginChunkedResponse(asyncsrv::T_application_octet_stream, generator);
	snprintf(fbuf, sizeof(fbuf), "attachment; filename=\"%s.gpx\"", idstr);
	response->addHeader(asyncsrv::T_Content_Disposition, fbuf);
	response->addHeader("X-Content-Type-Options", "nosniff");
	request->send(response);
}

static void noContent(AsyncWebServerRequest *request)
{
	request->send(http_NOCONTENT);
}

void setupWebServer()
{
	semDL = xSemaphoreCreateBinary();
	xSemaphoreGive(semDL);

	DefaultHeaders::Instance().addHeader(asyncsrv::T_CORS_ACAO, "*");
	DefaultHeaders::Instance().addHeader(asyncsrv::T_CORS_ACAM, "GET, POST, DELETE, OPTIONS");
	DefaultHeaders::Instance().addHeader(asyncsrv::T_CORS_ACAH, "Content-Type");

	server.addMiddleware([](AsyncWebServerRequest *request, auto next)
						 {
							 boost(true);
							 next(); // Lässt den Request zum eigentlichen Ziel weiterwandern
						 });

	server.on("/events", HTTP_OPTIONS, noContent);
	server.on("/", HTTP_OPTIONS, noContent);

	/**********************/

	server.on("/set", HTTP_POST, [](AsyncWebServerRequest *request)
			  {
				bool ok = false, prefs = false;

				if (const auto *p = request->getParam("mode"))
				{
					const auto mode = (log_mode_t)p->value().toInt();
					if (mode >= NOLOG && mode <= LOGAUTOSTART && logMode != mode)
					{
						logMode = mode;
						ok = prefs = true;
					}
				}

				if (const auto *p = request->getParam("append"))
				{
					const auto append = p->value().toInt();
					if (append >= 0 && append <= 1 && append != logAppend)
					{
						logAppend = append;
						ok = prefs = true;
					}
				}

				if (const auto *p = request->getParam("active"))
				{
					const auto cmdNeu = (log_cmd_t)p->value().toInt();
					if (logCmd == NOPE && cmdNeu >= STOPNOW && cmdNeu <= STARTNOW)
					{
						logCmd = cmdNeu; // Cmd wird nun im Haupttask ausgeführt.
						ok = true;
					}
				}

				// WiFi-Einstellungen
				bool wifiChanged = false;
				for (uint i = 0; i < wifiCreds.capacity(); ++i) {
					char ssid[15];
					char pass[15];
					snprintf(ssid,  sizeof(ssid), "wifi%d", i);
					const auto *pssid = request->getParam(ssid);
					if (pssid) {
						snprintf(pass,  sizeof(pass), "pass%d", i);
						const auto *ppass = request->getParam(pass);
						if (ppass) {
							wifiCredentials_t e = {};
							const auto &vpass = ppass->value();
							const auto &vssid = pssid->value();
							if (vssid.length() > 0 && vpass.length() > 0) {
								strlcpy(e.ssid, vssid.c_str(), sizeof(e.ssid));
								strlcpy(e.pass, vpass.c_str(), sizeof(e.pass));
							}
							if (i < wifiCreds.size())
								wifiCreds[i] = e;
							else
								wifiCreds.push_back(e);
							ok = prefs = wifiChanged = true;
						}
					}
				}

				request->send(ok ? http_OK : http_BADREQUEST);
				if (ok && prefs)
					savePrefs();
				if (ok && wifiChanged) {
					uiSendJson(false, true); } });

	/**********************/
	server.on("/delete", HTTP_OPTIONS, noContent);
	server.on("/delete", HTTP_DELETE, [](AsyncWebServerRequest *request)
			  {
				const auto *param = request->getParam("file");
				if (!param) {
						request->send(http_BADREQUEST);
						return;
					}
				const auto p = atoll(param->value().c_str());
				size_t cnt;
				if (p == -1)
					cnt = deleteAllFiles();
				else
					cnt = deleteFile(p);
				request->send(cnt ? http_OK : http_ERROR);
				if (cnt) uiSendJson(true, false, true); });

	/**********************/

	server.on("/download", HTTP_GET, download);

	/**********************/
	server.serveStatic("/", LittleFS, "/web/")
		.setDefaultFile("index.html")
		.setCacheControl(cachectrl);

	events.onConnect([](AsyncEventSourceClient *client)
					 {
						boost(true);
						client->send("{}", "message");
						yield();
						uiSendJson(true, true, true);
						logi("SSE client connected"); });

	events.onDisconnect([](const auto *cb)
						{ logv("SSE Client Disconnect"); });

	server.addHandler(&events);
	server.begin();
}
