#include "includes.h"
/****************************************************************************************************************************/
#if GPS_MODEL == UBLOX

static constexpr uint8_t SYNC[2] = {0xb5, 0x62};

// UBX Protocol: Page 185
static void sendUBXCommand(const uint8_t *cmd, const size_t size)
{
	if (cmd == nullptr || size < 4)
		return;

	// Sync schreiben:
	GPSSerial.write((const uint8_t*)&SYNC, sizeof(SYNC));

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
		GPSSerial.write((uint8_t)0);
		ckB = ckB + ckA;
	}

	GPSSerial.write(ckA);
	GPSSerial.write(ckB);
	GPSSerial.flush();
}

/****************************************************************************************************************************/

void hwinit()
{
	logv("GPS HW init.");

	if (0) //  Warmstart. Eigentlich nicht notwendig. Setzt die Konfig auch nicht zurück.
	{
		constexpr uint8_t CFG_RST[] = {0x06, 0x04, 0x04, 0x00, 0x01, 0x00, 0x09};
		sendUBXCommand(CFG_RST, sizeof(CFG_RST));
		logi("Ublox Warmstart");
		delay(500);
	}

	constexpr uint8_t CFG_RATE[] = {
		0x06, 0x08, 0x06, 0x00, // CFG-RATE, Payload length 6
		200, 0,					// measRate = 200 ms
		5, 0,					// navRate = 5 -> (200ms * 5 = 1 Sekunde)
		1, 0					// timeRef = 1 (GPS time)
	};
	sendUBXCommand(CFG_RATE, sizeof(CFG_RATE));

	// Page 231, Navigation engine settings  UBX-CFG-NAV5

	constexpr uint8_t CFG_NAV5[] = {
		0x06, 0x24, 0x24, 0x00,
		0xFF, 0xFF,				// Mask = alles
		4,						// Dynamic platform model: Automotive Default: 0
		0x03,					// Auto 2d/3d
		0x00, 0x00, 0x00, 0x00, // fixed Alt
		0x10, 0x27, 0x00, 0x00, // fixed Alt var
		0x05,					// min Elevation °
		0x00,					// Reserved
		0xFA, 0x00,				// pDop Mask
		0xFA, 0x00,				// tDop Mask
		100, 0x00,				// min Position Accuracy (m) Default: 100m
		0x5E, 0x01,				// Time Accuracy (350)
		50,						// static Hold theshold (cm/s) (0.5m/s) Default: 0
		0x3C,					// DGNSS timeout
		0x00,					// cnoThreshNumSVs
		0x00,					// cnoThresh
		0x00, 0x00,				// Reserved
		5, 0x00,				// staticHoldMax Dist (m) Default: 0
		0x00					// utc Standard
	};
	sendUBXCommand(CFG_NAV5, sizeof(CFG_NAV5));

	// NMEA - unbenötigte Nachrichten abschalten
	constexpr uint8_t CFG_MSG_$GNGLL[] = {0x06, 0x01, 0x08, 0x00, 0xF0, 0x01};
	sendUBXCommand(CFG_MSG_$GNGLL, sizeof(CFG_MSG_$GNGLL));

	constexpr uint8_t CFG_MSG_$GNGSA[] = {0x06, 0x01, 0x08, 0x00, 0xF0, 0x02};
	sendUBXCommand(CFG_MSG_$GNGSA, sizeof(CFG_MSG_$GNGSA));

	constexpr uint8_t CFG_MSG_$GPGSV[] = {0x06, 0x01, 0x08, 0x00, 0xF0, 0x03};
	sendUBXCommand(CFG_MSG_$GPGSV, sizeof(CFG_MSG_$GPGSV));

	constexpr  uint8_t CFG_MSG_$GNVTG[] = {0x06, 0x01, 0x08, 0x00, 0xF0, 0x05};
	sendUBXCommand(CFG_MSG_$GNVTG, sizeof(CFG_MSG_$GNVTG));
}

/*

* $GNRMC,201139.00,A,5120.97130,N,00638.94382,E,0.091,,191125,,,A*62
* $GNGGA,201139.00,5120.97130,N,00638.94382,E,1,06,3.43,41.7,M,46.4,M,,*7F
- $GNGSA,A,3,26,31,16,27,,,,,,,,,5.25,3.43,3.98*1C
- $GNGSA,A,3,76,86,,,,,,,,,,,5.25,3.43,3.98*17
- $GNGLL,5120.97130,N,00638.94382,E,201139.00,A,A*7C
*/

#else
void hwinit() {};
#endif // UBX
