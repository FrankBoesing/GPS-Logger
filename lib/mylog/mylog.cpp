#include <Arduino.h>
#include <stdarg.h>
#include "mylog.h"

#define LOG_MAGIC_NUMBER 0xDEADBEEF // Damit wir wissen, dass die Logs echt sind

RTC_NOINIT_ATTR char ram_log_buffer[RAM_LOG_SIZE];
RTC_NOINIT_ATTR int ram_log_idx;
RTC_NOINIT_ATTR uint32_t ram_log_magic;

static LogOutputCallback external_output = NULL;

void setLogOutputInterface(LogOutputCallback cb)
{
	external_output = cb;
}

// Erlaubt Zugriff auf den kompletten Puffer (linearisiert)
void getFullBuffer(void (*handler)(const char *data, size_t len))
{
	// Teil 1: Vom aktuellen Schreib-Index bis zum Ende des physischen Puffers
	// Das sind die "älteren" Daten im Ring
	const size_t len1 = RAM_LOG_SIZE - ram_log_idx;
	if (len1 > 0)
	{
		handler(ram_log_buffer + ram_log_idx, len1);
	}

	// Teil 2: Vom Anfang des Puffers bis zum aktuellen Schreib-Index
	// Das sind die "neuesten" Daten
	if (ram_log_idx > 0)
	{
		handler(ram_log_buffer, ram_log_idx);
	}
}

int custom_ram_printf(const char *fmt, ...)
{
	char loc_buf[128]; // Temporärer Puffer für eine Zeile
	va_list arg;
	va_start(arg, fmt);
	const int len = vsnprintf(loc_buf, sizeof(loc_buf), fmt, arg);
	va_end(arg);

	if (len > 0)
	{
		// 1. In den RAM-Ringpuffer kopieren
		for (int i = 0; i < len; i++)
		{
			ram_log_buffer[ram_log_idx] = loc_buf[i];
			ram_log_idx = (ram_log_idx + 1) % RAM_LOG_SIZE;
			// Puffer terminieren (optional für einfaches Drucken)
			ram_log_buffer[ram_log_idx] = '\0';
		}

		// 2. An das Interface senden (z.B. Telnet)
		if (external_output != NULL)
		{
			external_output(loc_buf);
		}

		// 3. In Serial ausgeben
		Serial.print(loc_buf);
	}
	return len;
}

static void clearRamLog()
{
	ram_log_idx = 0;
	ram_log_buffer[0] = '\0';
}

static void dumpRamLog()
{
	Serial.println("--- START RAM LOG ---");
	Serial.write(ram_log_buffer + ram_log_idx); // Teil nach dem Index (alt)
	Serial.write(ram_log_buffer, ram_log_idx);	// Teil vor dem Index (neu)
	Serial.println("\n--- END RAM LOG ---");
}

void initRamLogging()
{
	// Wenn die Magic Number nicht stimmt, wurde der RAM frisch initialisiert (Kaltstart)
	if (ram_log_magic != LOG_MAGIC_NUMBER)
	{
		memset(ram_log_buffer, 0, RAM_LOG_SIZE);
		ram_log_idx = 0;
		ram_log_magic = LOG_MAGIC_NUMBER;
		logi("RAM-Log neu initialisiert.");
	}
	else
	{
		// Alte Logs ausgeben, da sie einen Reset überlebt haben.
		Serial.println("--- ALTE LOGS GEFUNDEN (PRE-RESET) ---");
		dumpRamLog();
		clearRamLog();
	}
}
