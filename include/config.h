#ifndef CONFIG_H
#define CONFIG_H
#include <Arduino.h>
#include <LittleFS.h>
#include <esp_wifi.h>

#define TIMEZONE "CET-1CEST,M3.5.0/2,M10.5.0/3" // Zeitzone Berlin, Amsterdam

// ---------- GPS & Bewegung ----------

#define GPSSerial Serial1 // ublox gps: It is suggested to add a series resistor (500 ohm) to the TX line to suppress harmonics.
#define GPS_RX_PIN 20     // Default on ESP32-C3
#define GPS_TX_PIN 21     // Default on ESP32-C3
#define GPS_BAUD 9600
#define GPS_MODEL UBLOX
#define GPSWAITFORINITIALDATA (5UL * 1000UL) // Wartezeit nach Start für Lebenszeichen vom GPS

#define GPS_TIMESYNC_INTERVAL_MS (15UL * 60UL * 1000UL) // Alle 15 Minuten Uhrzeit vom GPS neu setzen (Einheit Millisekunden)
#define GPS_LOGINTERVAL 1UL                             // Logging alle 1 Sekunde
#define DEFAULTLOGMODE NoLog                            // (s.u.)
#define MIN_SPEED_TO_START 5.0f                         // Mindestgeschwindigkeit (KM/H) um loggen zu starten

// ---------- WiFi ----------

#define AP_SSID "GPS-Logger"
#define AP_PASS "12345678"
#define HOSTNAME "gps"
#define WiFI_MAX_POWER 8      // dBm, max 20 (ESP32-C3 Noname Boards oft am besten mit ~8 dBm. Höhere Werte bringen meist nichts)
#define WiFi_POWER_MODE WIFI_PS_MAX_MODEM

// ---------- Speicher ----------

#define FILES_WEB_DIR "/web/"
#define FILECACHE_MAXPOINTS 10

#define FILE_DONWNLOAD_NAME "%F_%H-%M.gpx" // Name der Downloads, stftime Format https://man7.org/linux/man-pages/man3/strftime.3.html
//#define FILE_DELETE_OLDEST true  // Falls der Platz im Flash knapp wird, älteste Datei löschen //TODO: noch nicht implementiert
//#define FILE_MIN_FREE_FLASH 4096 // Feier Speicher Mindestgröße

#define RESTART_AFTER_IDLE true         // Nach einer Fahrtpause an die letzte Datei anhängen?
#define MAX_IDLE_SECONDS (120UL * 60UL) // Max. Pausenlänge - danach wird eine neue Datei erstellt.

// ---------- System ----------
#define SERIAL_BAUD 115200

/*****************************************************************************************************/
extern File currentFile;
extern SemaphoreHandle_t semFile;
extern const size_t& fsTotalBytes;

enum eLogMode : uint8_t
{
  NoLog = 0,
  LogAfterBoot = 1,
  LogAfterMinSpeed = 2
};

enum eLogCmd: uint8_t
{
  nope = 0,
  stopNow = 1,
  startNow = 2
};

extern volatile eLogMode logMode;
extern volatile eLogCmd logCmd;

#define TIMEOFFSET 1761955200LL
typedef uint32_t time32_t;
struct GPSPoint
{
  float lat;
  float lon;
  time32_t time; // Zeit in 32 Bit speichern (von time_t wird TIMEOFFSET abgezogen)
};

#endif

/*****************************************************************************************************/

#if ESP_ARDUINO_VERSION < ESP_ARDUINO_VERSION_VAL(3, 2, 0)
#error "Wegen Nutzung von LittleFS Timestamps wird zwingend mindestens IDF 3.2 benötigt."
#endif
