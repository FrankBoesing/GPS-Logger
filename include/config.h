#ifndef CONFIG_H
#define CONFIG_H
#include <Arduino.h>
#include <LittleFS.h>
#include <esp_wifi.h>

// ---------- GPS & Bewegung ----------

#define GPSSerial Serial1 // ublox gps: It is suggested to add a series resistor (500 ohm) to the TX line to suppress harmonics.
#define GPS_RX_PIN 20     // Default on ESP32-C3
#define GPS_TX_PIN 21     // Default on ESP32-C3
#define GPS_BAUD 9600
#define GPS_MODEL UBLOX
#define GPSWAITFORINITIALDATA (5UL * 1000UL)            // Wartezeit nach Start für Lebenszeichen vom GPS
#define GPS_MIN_SATELLITES 4                            // Mindestanzahl Satelliten

#define DEFAULTLOGMODE NoLog                            // (s.u.)
#define MIN_SPEED_TO_START 5.0f                         // Mindestgeschwindigkeit (KM/H) um loggen zu starten

// ---------- WiFi ----------

#define AP_SSID "GPS-Logger"
#define AP_PASS "12345678"
#define HOSTNAME "gps"

// ---------- Speicher ----------

#define FILES_WEB_DIR "/web/"
#define FILECACHE_MAXPOINTS 5

#define FILE_DONWNLOAD_NAME "%F_%H-%M.gpx" // Name der Downloads, stftime Format https://man7.org/linux/man-pages/man3/strftime.3.html

#define RESTART_AFTER_IDLE true         // Nach einer Fahrtpause an die letzte Datei anhängen?
#define MAX_IDLE_SECONDS (120UL * 60UL) // Max. Pausenlänge - danach wird eine neue Datei erstellt.

// ---------- System ----------
#define SERIAL_BAUD 115200
#define COMPRESSION_ZIGZAG_VARINT true
#define CPU_FREQ_SLOW 80      // MHz
#define WiFI_MAX_POWER 8      // dBm, max 20 (ESP32-C3 Noname Boards oft am besten mit ~8 dBm. Höhere Werte bringen meist nichts)
#define WiFi_POWER_MODE WIFI_PS_MAX_MODEM

/*****************************************************************************************************/

#if ESP_ARDUINO_VERSION < ESP_ARDUINO_VERSION_VAL(3, 2, 0)
#error "Wegen Nutzung von LittleFS Timestamps wird zwingend mindestens IDF 3.2 benötigt."
#endif

#endif
