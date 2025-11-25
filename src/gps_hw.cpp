#include "gps_hw.h"
#include "debug.h"


/****************************************************************************************************************************/
#if GPS_MODEL == UBLOX

#define SHOW_ACKNACK (CORE_DEBUG_LEVEL >= ARDUHAL_LOG_LEVEL_DEBUG) // Nur für Debug nützlich. Zeigt Response auf UBX-Commands an.

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

static void ubxsend(const ubxfmt ubx)
{
    uint8_t bmessage[64];
    const uint16_t len = ubx.len;

    if (len > sizeof(bmessage) - 8) return;

    bmessage[0] = sync1;
    bmessage[1] = sync2;
    bmessage[2] = ubx.ubxclass;
    bmessage[3] = ubx.id;
    bmessage[4] = (uint8_t)(len & 0xFF);
    bmessage[5] = 0; // (uint8_t)(len >> 8);

    //Message mit Payload und danach 0 auffüllen:
    for (int i = 0; i < len; i++)
    {
        bmessage[6 + i] = (i < ubx.payloadused) ? ubx.data[i] : 0x0;
    }

    // Checksumme berechnen:
    uint8_t ck_a = 0, ck_b = 0;
    for (int i = 2; i < 6 + len; i++)
    {
        ck_a += bmessage[i];
        ck_b += ck_a;
    }
    bmessage[6 + len] = ck_a;
    bmessage[7 + len] = ck_b;

    GPSSerial.write(bmessage, 8 + len);
    GPSSerial.flush();
}

bool handleHwData(char ch)
{
    if (!SHOW_ACKNACK) return false;

    constexpr size_t len_ack_nack = 9;
    static uint8_t buf[len_ack_nack];
    static int hwcnt = -1;

    if (ch == sync1) {
        hwcnt = 0;
        return true;
    }

    if (hwcnt >= 0 && hwcnt < sizeof(buf)) {
        buf[hwcnt++] = ch;
        if (hwcnt == sizeof(buf))
        {
            if (buf[0] == sync2 && buf[1] == 0x05) {
                log_d("UBX Class: 0x%02x ID: 0x%02x %cACK", buf[5], buf[6], buf[2] == 1 ? ' ' : 'N');
            }
            hwcnt = -1;
        }
        return true;
    }

    return false;
}

/****************************************************************************************************************************/
// UBX Commands beim Start

// Page 256, UBX-CFG-RST
static const uint8_t UBX_CFG_RST_data[] = {0xff, 0xff, 0x02, 00};
static const ubxfmt UBX_CFG_RST = {
    .ubxclass = 0x06,
    .id = 0x04,
    .len = 4,
    .payloadused = sizeof(UBX_CFG_RST_data),
    .data = (uint8_t *)UBX_CFG_RST_data};

// Page 231, Navigation engine setting  UBX-CFG-NAV5: Automotive
static const uint8_t UBX_CFG_NAV5_data[] = {1, 0, 4};
static const ubxfmt UBX_CFG_NAV5 = {
    .ubxclass = 0x06,
    .id = 0x24,
    .len = 36,
    .payloadused = sizeof(UBX_CFG_NAV5_data),
    .data = (uint8_t *)UBX_CFG_NAV5_data};

 //Satellitenübersicht abschalten
static const uint8_t UBX_CFG_MSG_$GPGSV_data[] = {0xF0, 0x03};
static const ubxfmt UBX_CFG_MSG_$GPGSV= {
    .ubxclass = 0x06,
    .id = 0x01,
    .len = 8,
    .payloadused = sizeof(UBX_CFG_MSG_$GPGSV_data),
    .data = (uint8_t *)UBX_CFG_MSG_$GPGSV_data};

//$GNVTG abschalten
static const uint8_t UBX_CFG_MSG_$GNVTG_data[] = {0xF0, 0x05};
static const ubxfmt UBX_CFG_MSG_$GNVTG= {
    .ubxclass = 0x06,
    .id = 0x01,
    .len = 8,
    .payloadused = sizeof(UBX_CFG_MSG_$GNVTG_data),
    .data = (uint8_t *)UBX_CFG_MSG_$GNVTG_data};

//$GNGSA abschalten
static const uint8_t UBX_CFG_MSG_$GNGSA_data[] = {0xF0, 0x02};
static const ubxfmt UBX_CFG_MSG_$GNGSA= {
    .ubxclass = 0x06,
    .id = 0x01,
    .len = 8,
    .payloadused = sizeof(UBX_CFG_MSG_$GNGSA_data),
    .data = (uint8_t *)UBX_CFG_MSG_$GNGSA_data};

//$GNGLL abschalten
static const uint8_t UBX_CFG_MSG_$GNGLL_data[] = {0xF0, 0x01};
static const ubxfmt UBX_CFG_MSG_$GNGLL= {
    .ubxclass = 0x06,
    .id = 0x01,
    .len = 8,
    .payloadused = sizeof(UBX_CFG_MSG_$GNGLL_data),
    .data = (uint8_t *)UBX_CFG_MSG_$GNGLL_data};


/****************************************************************************************************************************/

void hwinit()
{
    log_v("GPS HW init.");

    //Besondere Config setzen
    ubxsend(UBX_CFG_NAV5);
    //Unebnötigte Nachrichten abschalten. TinGPSPlus nutzt nur $GNRMC und $GNGGA
    ubxsend(UBX_CFG_MSG_$GPGSV); // GSV
    ubxsend(UBX_CFG_MSG_$GNVTG); // VTG
    ubxsend(UBX_CFG_MSG_$GNGSA); // GSA
    ubxsend(UBX_CFG_MSG_$GNGLL); // GLL

    if (0) //Nur für Debugzwecke
        ubxsend(UBX_CFG_RST);
}


/*

* $GNRMC,201139.00,A,5120.97130,N,00638.94382,E,0.091,,191125,,,A*62
* $GNGGA,201139.00,5120.97130,N,00638.94382,E,1,06,3.43,41.7,M,46.4,M,,*7F
- $GNGSA,A,3,26,31,16,27,,,,,,,,,5.25,3.43,3.98*1C
- $GNGSA,A,3,76,86,,,,,,,,,,,5.25,3.43,3.98*17
- $GNGLL,5120.97130,N,00638.94382,E,201139.00,A,A*7C
* $GNTXT,01,01,01,More than 100 frame errors, UART RX was disabled*70

*/
//UBX - End

#else
void hwinit() {};
bool handleHwData(char ch);
#endif //UBX
