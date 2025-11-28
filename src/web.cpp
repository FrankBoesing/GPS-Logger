#include "web.h"

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

#include "utils.h"
#include "debug.h"

extern char gpsFixQuality;
void savePrefs();

static constexpr const char gpxheader[] = "<?xml version=\"1.0\" encoding=\"ISO-8859-1\" standalone=\"no\"?>\n<gpx version=\"1.1\" creator=\"gpx Logger\">\n<trk><trkseg>\n";
static constexpr const char gpxfooter[] = "</trkseg></trk>\n</gpx>\n";
static constexpr const char _argFile[] = "file";

static AsyncWebServer server(80);
static AsyncCorsMiddleware cors;

static SemaphoreHandle_t semDL; // Download

/****************************************************************************************************************************/
/****************************************************************************************************************************/

static bool isBadRequest(AsyncWebServerRequest *request, const char *arg)
{
  bool r = !request->hasParam(arg);
  if (r)
  {
    request->send(400, "Bad Request.");
  }
  return r;
}

/****************************************************************************************************************************/

void setupWebServer()
{
  semDL = xSemaphoreCreateBinary();
  xSemaphoreGive(semDL);

  // https://github.com/ESP32Async/ESPAsyncWebServer/blob/main/examples/Json/Json.ino

  // List Files
  server.on("/files", HTTP_GET, [](AsyncWebServerRequest *request)
            {

              AsyncJsonResponse *response = new AsyncJsonResponse();
              JsonObject fileList = response->getRoot().to<JsonObject>();

              readFileList(fileList);
              response->setLength();
              request->send(response); });

  /**********************/

  server.on("/info", HTTP_GET, [](AsyncWebServerRequest *request)
            {
              AsyncJsonResponse *response = new AsyncJsonResponse();
              JsonObject doc = response->getRoot().to<JsonObject>();

              char quality[2] = "";
              quality[0] = gpsFixQuality;

              time_t now;
              time(&now);
              doc["time_t"]  = now;

              doc["gpsQuality"] = quality;
              // doc["uptime"] = millis();

              doc["logMode"] = (int)logMode;

              doc["totalBytes"] = fsTotalBytes;
              doc["usedBytes"] = LittleFS.usedBytes();
              //doc["sizePoint"] = sizeof(GPSPoint);

              doc["loggingActive"] = (bool)logfile;

              multi_heap_info_t info;
              heap_caps_get_info(&info, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT); // internal RAM
              doc["RAMtotalFree"] = info.total_free_bytes;
              doc["RAMminFree"] = info.minimum_free_bytes;
              doc["RAMlargestFreeBlock"] = info.largest_free_block;

              response->setLength();
              request->send(response); });

  /**********************/

  // Set logMode in preferences
  server.on("/setlogmode", HTTP_GET, [](AsyncWebServerRequest *request)
            {
              if (isBadRequest(request,"logMode")) return;

              eLogMode mode = (eLogMode)request->getParam("logMode")->value().toInt();
              if (mode <= LogAfterMinSpeed) {
                logMode = mode;
                savePrefs();
              }
              else request->send(400, "texte/plain", "Error");
              request->send(200, "texte/plain", "OK"); });

  // Sofortige Aktionen
  server.on("/setlogactive", HTTP_GET, [](AsyncWebServerRequest *request)
            {
              if (isBadRequest(request,"logActive")) return;

              eLogCmd mode = (eLogCmd)request->getParam("logActive")->value().toInt();
              if (logCmd==nope &&
                  ((mode == startNow && gpsFixQuality > '0') || (mode == stopNow)) ) {

                logCmd = mode; // logCMD wird nun im Hauptprogramm ausgeführt.

#if 1
                LEDON();
                do { // Kein Timeout nötig.. wenn was schief läuft, ist sowieso alles kaputt. So merkt der Anwender es wenigstens schnell.
                  vTaskDelay(20 / portTICK_PERIOD_MS);
                } while(logCmd != nope);
                LEDOFF();
#endif

              } else
                request->send(400, "text/plain", "Error");
              request->send(200, "text/plain", "OK"); });

  /**********************/

  server.on("/delete", HTTP_GET, [](AsyncWebServerRequest *request)
            {
              if (isBadRequest(request, _argFile)) return;
              String arg = request->getParam(_argFile)->value();
              deleteFiles( arg.c_str() );
              request->send(200, "text/plain", "OK"); });

  /**********************/

  /*Chunked Download mit
    - Konvertierung vom Binär- ins xml Format
    - Doppelpufferung
  */
  server.on("/download", HTTP_GET, [](AsyncWebServerRequest *request)
            {
              if (isBadRequest(request, _argFile)) return;

              String pathParam = FILE_PREFIX + request->getParam(_argFile)->value();
              const char *path = pathParam.c_str();

              if (logfile.isActive(path)) {
                request->send(409, "text/plain", "File is currently being written to");
                return;
              }

              if (!LittleFS.exists(path))
              {
                request->send(404, "text/plain", "File not found");
                return;
              }

              xSemaphoreTake(semDL, portMAX_DELAY);
              esp_wifi_set_ps(WIFI_PS_NONE);

              //constexpr size_t CHUNK_SIZE = TCP_MSS; // 1436
              constexpr size_t CHUNK_SIZE = 1460;
              constexpr uint NUM_BUF = 2;
              enum states {header, points, footer, done};

              struct DContext {
                logfileR f;
                states state;
                uint bufIdx;
                size_t len[NUM_BUF];
                char buf[NUM_BUF][CHUNK_SIZE];
              };

              DContext *ctx = new DContext{path, header,  0, {{0}, {0}}, {{0}, {0}}};
              //DContext *ctx = new DContext{std::move(f), header,  0, {{0}, {0}}, {{0}, {0}}};

              // --- Buffer-Füllfunktion ---
              auto fillBuffer = [ctx](char *buf, size_t bufSize) -> size_t {

                char tmp[512]; //Aufpassen dass ein Trackpoint hier rein passt (unten snprintf)
                constexpr size_t tmpSize = sizeof(tmp);
                size_t pos = 0;

                //Statusmaschine für die Abfolge header->trackpoints->footer->done
                switch (ctx->state)
                {
                  case header: {
                            const size_t hlen = strlen(gpxheader);
                            if (hlen < bufSize) {
                              memcpy(buf, gpxheader, hlen);
                              pos = hlen;
                            }
                            ctx->state = points;
                          } [[fallthrough]];

                  case points: {
                            //Eine Zeile sollte höchstens diese Länge haben. Bei Änderung von <trkpt> immer prüfen!
                            constexpr size_t lineLength = 80;

                            while (pos < bufSize - lineLength && ctx->f.available()) {
                              GPSPoint point;
                              if (!ctx->f.readPoint(&point)) break;

                              struct tm *tm;
                              time_t  ptime = (time_t)TIMEOFFSET + (time_t)point.time;
                              tm = gmtime(&ptime);

                              int len = snprintf(tmp, tmpSize, "<trkpt lat=\"%.6f\" lon=\"%.6f\">",point.lat, point.lon);
                              len += strftime(tmp + len, tmpSize - len, "<time>%FT%TZ</time></trkpt>\n", tm);
                              if (pos + len >= bufSize) break;

                              memcpy(buf + pos, tmp, len);
                              pos += len;
                            }
                            if (!ctx->f.available())
                              ctx->state = footer;
                            else
                              break;
                          } [[fallthrough]];

                  case footer: {
                            const size_t flen = strlen(gpxfooter);
                            if (pos + flen < bufSize) {
                              memcpy(buf + pos, gpxfooter, flen);
                              pos += flen;
                              ctx->state = done;
                            }
                          }

                  case done: {}

                } // switch

                return pos;
              }; // fillBuffer()

              // --- Erstbefüllung ---
              ctx->len[0] = fillBuffer(ctx->buf[0], sizeof(ctx->buf[0]));
              ctx->len[1] = 0;

              auto generator = [ctx, fillBuffer](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
                if (!ctx) return 0;

                char *active = ctx->buf[ctx->bufIdx];
                size_t activeLen = ctx->len[ctx->bufIdx];

                if (activeLen == 0 && ctx->state == done) {
                  delete ctx;
                  return 0;
                }

                size_t toSend = min(activeLen, maxLen);
                memcpy(buffer, active, toSend);

                if (toSend >= activeLen) {

                  uint idx = (1 + ctx->bufIdx) & (NUM_BUF-1); // Buffer wechseln
                  ctx->len[idx] = fillBuffer(ctx->buf[idx], sizeof(ctx->buf[0]));
                  ctx->bufIdx = idx;

                } else {
                  if (activeLen > toSend)
                    memmove(active, active + toSend, activeLen - toSend);
                  ctx->len[ctx->bufIdx] -= toSend;
                }

                return toSend;
              }; // generator

              AsyncWebServerResponse *response =
                  request->beginChunkedResponse("application/gpx+xml", generator);

              //Lesbaren Dateinamen erzeugen:
              time_t _time;
              str_to_ll(path + strlen(FILE_PREFIX), &_time); //FILE_PREFIX überspringen
              struct tm _t = *localtime(&_time);

              char dlname[64];
              strftime(dlname, sizeof(dlname), "attachment; filename=\"" FILE_DONWNLOAD_NAME "\"", &_t);

              response->addHeader("Content-Disposition", dlname);
              request->send(response);
              esp_wifi_set_ps(WiFi_POWER_MODE);
              xSemaphoreGive(semDL); });

  /**********************/

  server.on("/downloadraw", HTTP_GET, [](AsyncWebServerRequest *request)
            {
              if (isBadRequest(request, _argFile))
                return;

              String pathParam = FILE_PREFIX + request->getParam(_argFile)->value();
              const char *path = pathParam.c_str();

              if (logfile.isActive(path))
              {
                request->send(409, "text/plain", "File is currently being written to");
                return;
              }
              request->send(LittleFS, path , "application/octet-stream", true); });

  /**********************/
  // Static files
  // server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) { request->redirect("/index.html"); });
  server.serveStatic("/", LittleFS, "/web").setDefaultFile("index.html");
  // server.onNotFound([](AsyncWebServerRequest *request){ request->send(404, _texthtml, _notFound); });

  if (CORE_DEBUG_LEVEL > ARDUHAL_LOG_LEVEL_WARN)
    cors.setAllowCredentials(false); // for debug only

  server.addMiddleware(&cors);

  server.begin();
}
