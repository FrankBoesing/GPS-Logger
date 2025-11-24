#include "debug.h"

#include <TinyGPSPlus.h>
extern TinyGPSPlus gps;
extern TinyGPSLocation::Quality gpsFixQuality;


/****************************************************************************************************************************/
/****************************************************************************************************************************/

void logGPSInfo()
{
    static unsigned long GPSSentences = 0;
    static unsigned long lastPrint = 0;

    GPSSentences++;

    if (CORE_DEBUG_LEVEL < ARDUHAL_LOG_LEVEL_INFO)
        return;

    unsigned long t = millis();
    if (t - lastPrint < 900ul)
        return;
    lastPrint = t;

    time_t now;
    struct tm tt;
    char buf[32];
    time(&now);
    localtime_r(&now, &tt);
    strftime(buf, sizeof(buf), "%d.%m.%y %T", &tt);
    log_i("#%lu %s  Lat: %.5f Lon: %.5f   %.1f km/h Fix: %c", GPSSentences, buf, gps.location.lat(), gps.location.lng(), gps.speed.kmph(), gpsFixQuality);
}

// Zeigt GPS Rohdaten wenn kein Fix vorliegt. Sonst Info (s.o.)
void printSerialData(const char ch)
{

    if (CORE_DEBUG_LEVEL < ARDUHAL_LOG_LEVEL_VERBOSE)
        return;

    static char buf[128];
    static int buflen = 0;

    if (gpsFixQuality > '0') {
        buflen = 0;
        return;
    }

        // Buffer füllen
    if (buflen < sizeof(buf) - 2) // Platz für \0
        buf[buflen++] = ch;

    // Wenn Zeilenende oder Puffer voll → senden
    if (ch == '\n' || buflen >= sizeof(buf) - 2)
    {
        buf[buflen] = '\0';
        Serial.write(buf, buflen);
        buflen = 0;
        return;
    }

}

/****************************************************************************************************************************/
/****************************************************************************************************************************/

