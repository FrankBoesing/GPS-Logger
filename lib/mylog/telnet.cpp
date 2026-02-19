#include "mylog.h"
#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <AsyncTelnet.h> // AsyncTelnet

#pragma GCC push_options
#pragma GCC optimize("Os")

static AsyncTelnet logsrv;

static const constexpr uint8_t telnet8BitMode[] = {
	255, 251, 1, // IAC WILL ECHO
	255, 251, 3, // IAC WILL SUPPRESS GO AHEAD
	255, 252, 34 // IAC WONT LINEMODE
};

static void conn_cb_f(void *, AsyncClient *c)
{
	if (!logsrv.connected())
		return;

	logsrv.write((const char *)telnet8BitMode, sizeof(telnet8BitMode));
	logsrv.write("Welcome.\r\nHistory:\r\n");
	getFullBuffer([](const char *data, size_t len)
				  {if(data) logsrv.write(data, len); });
	logsrv.write("\r\nEnd History / Start Live:\r\n");
}

static void telnetForwarder(const char *line)
{
	if (!logsrv.connected())
		return;

	logsrv.write(line);
}

void initTelnetLogging()
{
	setLogOutputInterface(telnetForwarder);
	logsrv.onConnect(conn_cb_f);
	logsrv.begin();
}

#pragma GCC pop_options
