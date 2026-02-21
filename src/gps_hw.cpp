#include "includes.h"

constexpr int GPS_TARGET_BAUD = 115200;
constexpr int BAUDS[] = {115200, 9600, 38400, 57600, 4800};

static int setGPSSerialBaud(int baud)
{
	GPSSerial.begin(baud, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
	return baud;
}

static void endSerial()
{
	GPSSerial.end();
	delay(10); // Kurze Beruhigungspause für die Hardware
}

static int findBauds()
{
	for (int b : BAUDS)
	{
		logd("GPS Teste %d bit/s", b);
		setGPSSerialBaud(b);

		unsigned long start = millis();
		char lastChar = 0;

		while (millis() - start < 1200)
		{
			if (GPSSerial.available())
			{
				char c = GPSSerial.read();
				if (lastChar == '\n' && c == '$')
					return b;
				lastChar = c;
			}
			yield();
		}
		endSerial();
	}
	return 0;
}

/****************************************************************************************************************************/
#if GPS_MODEL == UBLOX

static constexpr uint8_t SYNC[2] = {0xb5, 0x62};

// UBX Protocol: Page 185
static void sendUBXCommand(const uint8_t *cmd, const size_t size)
{
	if (cmd == nullptr || size < 4)
		return;

	// Sync schreiben:
	GPSSerial.write((const uint8_t *)&SYNC, sizeof(SYNC));

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
	yield();
	GPSSerial.flush();
}

/****************************************************************************************************************************/

constexpr uint8_t CFG_PRT_BAUD[] = {
	0x06, 0x00,				// Class, ID (CFG-PRT)
	0x14, 0x00,				// Length (20 bytes)
	0x01,					// portID = UART1
	0x00,					// reserved
	0x00, 0x00,				// txReady
	0xD0, 0x08, 0x00, 0x00, // mode = 0x08D0 (8N1)
	(uint8_t)(GPS_TARGET_BAUD & 0xFF), (uint8_t)((GPS_TARGET_BAUD >> 8) & 0xFF),
	(uint8_t)((GPS_TARGET_BAUD >> 16) & 0xFF), (uint8_t)((GPS_TARGET_BAUD >> 24) & 0xFF),
	0x07, 0x00, // inProtoMask
	0x07, 0x00, // outProtoMask
	0x00, 0x00, // flags
	0x00, 0x00	// reserved
};

constexpr uint8_t CFG_RATE[] = {
	0x06, 0x08, 0x06, 0x00, // CFG-RATE, Payload length 6
	200, 0,					// measRate = 200 ms
	5, 0,					// navRate = 5 -> (200ms * 5 = 1 Sekunde)
	1, 0					// timeRef = 1 (GPS time)
};

constexpr uint8_t CFG_NAV5[] = {
	// Page 231, Navigation engine settings  UBX-CFG-NAV5
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

constexpr uint8_t CFG_MSG_$GNGLL[] = {0x06, 0x01, 0x08, 0x00, 0xF0, 0x01};
constexpr uint8_t CFG_MSG_$GNGSA[] = {0x06, 0x01, 0x08, 0x00, 0xF0, 0x02};
constexpr uint8_t CFG_MSG_$GPGSV[] = {0x06, 0x01, 0x08, 0x00, 0xF0, 0x03};
constexpr uint8_t CFG_MSG_$GNVTG[] = {0x06, 0x01, 0x08, 0x00, 0xF0, 0x05};

bool hwinit()
{
	logd("GPS HW init.");

	// 1. Baudrate finden
	int baud = findBauds();

	if (baud == 0)
	{
		loge("No GPS found.");
		return false;
	}

	// 2. Konfiguration nur wenn Baudrate nicht dem Ziel entspricht
	if (baud < GPS_TARGET_BAUD)
	{
		if (baud < GPS_TARGET_BAUD)
		{
			logd("Switching from %d to %d...", baud, GPS_TARGET_BAUD);
			sendUBXCommand(CFG_PRT_BAUD, sizeof(CFG_PRT_BAUD));
			endSerial();
			setGPSSerialBaud(GPS_TARGET_BAUD);
			GPSSerial.setTimeout(1200);
			if (!GPSSerial.find("$"))
			{
				loge("Switching failed");
				endSerial();
				setGPSSerialBaud(baud);
				return true; // dann bleiben wir halt bei der alten baudrate...
			}
			baud = GPS_TARGET_BAUD;
		}
	}

	logi("Using baud: %d", baud);

	// Weitere Konfigurationen senden
	logd("Sending UBX Configuration...");
	sendUBXCommand(CFG_RATE, sizeof(CFG_RATE));
	sendUBXCommand(CFG_NAV5, sizeof(CFG_NAV5));

	// Unnötige NMEA Nachrichten deaktivieren
	sendUBXCommand(CFG_MSG_$GNGLL, sizeof(CFG_MSG_$GNGLL));
	sendUBXCommand(CFG_MSG_$GNGSA, sizeof(CFG_MSG_$GNGSA));
	sendUBXCommand(CFG_MSG_$GPGSV, sizeof(CFG_MSG_$GPGSV));
	sendUBXCommand(CFG_MSG_$GNVTG, sizeof(CFG_MSG_$GNVTG));

	return true;
}

/*

* $GNRMC,201139.00,A,5120.97130,N,00638.94382,E,0.091,,191125,,,A*62
* $GNGGA,201139.00,5120.97130,N,00638.94382,E,1,06,3.43,41.7,M,46.4,M,,*7F
- $GNGSA,A,3,26,31,16,27,,,,,,,,,5.25,3.43,3.98*1C
- $GNGSA,A,3,76,86,,,,,,,,,,,5.25,3.43,3.98*17
- $GNGLL,5120.97130,N,00638.94382,E,201139.00,A,A*7C
*/

#else
bool hwinit() { return findBauds() > 0; };
#endif // UBX
