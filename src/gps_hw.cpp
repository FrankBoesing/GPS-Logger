#include "gps_hw.h"
#include "debug.h"

/****************************************************************************************************************************/
#if GPS_MODEL == UBLOX

static constexpr const uint8_t sync1 = 0xb5;
static constexpr const uint8_t sync2 = 0x62;

// UBX Protocol: Page 185

struct __attribute__((packed)) ubxfmt
{
    uint8_t ubxclass;
    uint8_t id;
    uint8_t len;
    uint8_t payloadused;
    uint8_t *data;
};

static bool ubxAckNack()
{
    constexpr const size_t MSGLEN = 10;
    unsigned long t = micros();
    do
    {
        if (micros() - t > 250ul * 1000ul) // 250 ms
            return false;
    } while (GPSSerial.read() != sync1);

    uint8_t buf[MSGLEN];
    bool ack = false;
    bool checksumOK = false;

    GPSSerial.readBytes(buf + 1, MSGLEN - 1);
    if (buf[1] == sync2 && buf[2] == 0x05)
    {
        uint8_t ckA = 0, ckB = 0;
        for (size_t i = 2; i < 8; i++) // Checksumme prüfen
        {
            ckA = ckA + buf[i];
            ckB = ckB + ckA;
        }
        checksumOK = (ckA == buf[8]) && (ckB == buf[9]);
        ack = (buf[3] == 0x01);
        log_d("UBX Class:0x%02x ID:0x%02x Checksum:%s %cACK", buf[6], buf[7], checksumOK ? "ok" : "ERROR", ack ? ' ' : 'N');
    }
    else
        return false;
    return checksumOK && ack;
}

static bool sendUBXCommand(const uint8_t *cmd, const size_t size, const bool wait = true)
{
    if (cmd == nullptr || size < 4)
        return false;

    // Sync schreiben:
    GPSSerial.write((uint8_t)sync1);
    GPSSerial.write((uint8_t)sync2);

    const size_t lenPayload = ((size_t)cmd[3] << 8) | (size_t)cmd[2];
    uint8_t ckA = 0, ckB = 0;

    size_t idx = 0;
    for (; idx < size; ++idx)
    {
        GPSSerial.write(cmd[idx]);
        ckA = ckA + cmd[idx];
        ckB = ckB + ckA;
    }

    for (; idx < lenPayload + 4; ++idx) // Mit 0 auffüllen falls nötig
    {
        GPSSerial.write(0);
        ckB = ckB + ckA;
    }

    GPSSerial.write(ckA);
    GPSSerial.write(ckB);
    GPSSerial.flush();
    if (wait)
        return ubxAckNack();
    return true;
}

/****************************************************************************************************************************/

void hwinit()
{
    log_v("GPS HW init.");

    if (0) //  Factory Reset. Nur für Debugzwecke.
    {
        const uint8_t CFG_RST[] = {0x06, 0x04, 0x04, 0x00, 0xFF, 0xFF, 0x09};
        sendUBXCommand(CFG_RST, sizeof(CFG_RST), false);
        delay(250);
    }

    if (0) //  Warmstart. Eigentlich nicht notwendig. Setzt die Konfig auch nicht zurück.
    {
        const uint8_t CFG_RST[] = {0x06, 0x04, 0x04, 0x00, 0x01, 0x00, 0x09};
        sendUBXCommand(CFG_RST, sizeof(CFG_RST), false);
        delay(250);
    }

    bool err = false;

    // Page 231, Navigation engine setting  UBX-CFG-NAV5
    /* Dynamic platform model: Automotive */
    const uint8_t CFG_NAV5_Automotive[] = {0x06, 0x24, 0x24, 0x00, 0x01, 0x00, 0x04};
    err |= !sendUBXCommand(CFG_NAV5_Automotive, sizeof(CFG_NAV5_Automotive));

#if 1 // Position-Output-Filter (pAcc) + staticHold
    /*
        Static-Hold einschalten, Schwelle = 0.5 m/s (50 cm/s) (staticHoldThr), Abbruch-Distanz = 5 m (staticHoldMaxDist)
    */
    const uint8_t CFG_NAV5_Filter[] = {0x06, 0x24, 0x24, 0x00,
                                       0x40, 0x00, 0x04, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05};
    err |= !sendUBXCommand(CFG_NAV5_Filter, sizeof(CFG_NAV5_Filter));
#else
    /* Position-Accuracy pAcc=10 m;
       Static-Hold einschalten, Schwelle = 0.5 m/s (50 cm/s) (staticHoldThr), Abbruch-Distanz = 5 m (staticHoldMaxDist)
    */
    const uint8_t CFG_NAV5_Filter[] = {0x06, 0x24, 0x24, 0x00,
                                       0x50, 0x00, 0x04, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                       0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05};
    err |= !sendUBXCommand(CFG_NAV5_Filter, sizeof(CFG_NAV5_Filter));
#endif

    // NMEA - unbenötigte Nachrichten abschalten

    const uint8_t CFG_MSG_$GNGLL[] = {0x06, 0x01, 0x08, 0x00, 0xF0, 0x01};
    err |= !sendUBXCommand(CFG_MSG_$GNGLL, sizeof(CFG_MSG_$GNGLL));

    const uint8_t CFG_MSG_$GNGSA[] = {0x06, 0x01, 0x08, 0x00, 0xF0, 0x02};
    err |= !sendUBXCommand(CFG_MSG_$GNGSA, sizeof(CFG_MSG_$GNGSA));

    const uint8_t CFG_MSG_$GPGSV[] = {0x06, 0x01, 0x08, 0x00, 0xF0, 0x03};
    err |= !sendUBXCommand(CFG_MSG_$GPGSV, sizeof(CFG_MSG_$GPGSV));

    const uint8_t CFG_MSG_$GNVTG[] = {0x06, 0x01, 0x08, 0x00, 0xF0, 0x05};
    err |= !sendUBXCommand(CFG_MSG_$GNVTG, sizeof(CFG_MSG_$GNVTG));

    if (err)
        log_e("Error(s) sending GPS Config.");
}

/*

* $GNRMC,201139.00,A,5120.97130,N,00638.94382,E,0.091,,191125,,,A*62
* $GNGGA,201139.00,5120.97130,N,00638.94382,E,1,06,3.43,41.7,M,46.4,M,,*7F
- $GNGSA,A,3,26,31,16,27,,,,,,,,,5.25,3.43,3.98*1C
- $GNGSA,A,3,76,86,,,,,,,,,,,5.25,3.43,3.98*17
- $GNGLL,5120.97130,N,00638.94382,E,201139.00,A,A*7C
* $GNTXT,01,01,01,More than 100 frame errors, UART RX was disabled*70

*/
// UBX - End

#else
void hwinit() {};
#endif // UBX
