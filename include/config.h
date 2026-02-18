#ifndef CONFIG_H
#define CONFIG_H

// ---------- GPS & Bewegung ----------

#define GPSSerial Serial1 // ublox gps: It is suggested to add a series resistor (500 ohm) to the TX line to suppress harmonics.
#define GPS_RX_PIN 0	  // Default on ESP32-C3
#define GPS_TX_PIN 5	  // Pin21 sendet irgendwas beim booten.
#define GPS_BAUD 9600
#define GPS_MODEL UBLOX

#define GPS_MIN_SATELLITES 5 // Mindestanzahl Satelliten
#define GPS_MIN_HDOP 	2.8f
#define DEFAULTLOGMODE NOLOG

// ---------- WiFi ----------

#define AP_SSID "GPS-Logger"
#define AP_PASS "12345678"
#define HOSTNAME "gps"

// ---------- Speicher ----------

#define FILES_WEB_DIR "/web/"
#define FILECACHE_MAXPOINTS 5
#define FILECACHE_MAXAGE 5 // Sekunden (Nach x Sekunden spätestens flush)
#define MAX_IDLE_SECONDS (120UL * 60) // Max. Pausenlänge (120 Minuten) - danach wird eine neue Datei erstellt.

// ---------- System ----------
#define WIFI_MAX_NETWORKS 5
#define WiFI_MAX_POWER 6 // dBm, max 20 (ESP32-C3 Noname Boards oft am besten mit ~8 dBm. Höhere Werte bringen meist nichts)

#define ENABLE_HEAT_REDUCTION true

#if ENABLE_HEAT_REDUCTION
    #define CPU_FREQ_IDLE          80
#else
    #define CPU_FREQ_IDLE          CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ
#endif

#define CPU_FREQ_DEFAULT       CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ



/*****************************************************************************************************/

#if ESP_ARDUINO_VERSION < ESP_ARDUINO_VERSION_VAL(3, 2, 0)
#error "Wegen Nutzung von LittleFS Timestamps wird zwingend mindestens IDF 3.2 benötigt."
#endif

#endif
