#include <WiFi.h>
#include "mylog.h"

WiFiServer telnetServer(23);
WiFiClient telnetClient;

// Diese Funktion wird vom Logger aufgerufen, wenn eine neue Zeile kommt
void myTelnetForwarder(const char *line)
{
	if (telnetClient && telnetClient.connected())
	{
		telnetClient.print(line);
	}
}

void setup()
{
	initRamLogging(); // RTC-RAM vorbereiten

	// Das Interface verknüpfen
	setLogOutputInterface(myTelnetForwarder);

	// WiFi etc. starten...
	telnetServer.begin();
}

void loop()
{
	// Neuen Client akzeptieren
	if (telnetServer.hasClient())
	{
		if (!telnetClient || !telnetClient.connected())
		{
			telnetClient = telnetServer.available();
			telnetClient.println("--- Willkommen beim Live-Log ---");

			// OPTIONAL: Beim Verbinden sofort den kompletten Buffer senden (Punkt b)
			telnetClient.println("--- Historie: ---");
			getFullBuffer([](const char *data, size_t len)
						  {
                if(data) telnetClient.write(data, len); });
			telnetClient.println("\n--- Ende Historie / Start Live ---");
		}
	}
}
