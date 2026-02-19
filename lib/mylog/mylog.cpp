#include <Arduino.h>
#include <stdarg.h>
#include "mylog.h"

#pragma GCC push_options
#pragma GCC optimize("Os")

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
	char loc_buf[160]; // Temporärer Puffer für eine Zeile
	va_list arg;
	va_start(arg, fmt);
	const int len = vsnprintf(loc_buf, sizeof(loc_buf), fmt, arg);
	va_end(arg);

	if (len > 0)
	{
		// 1. In Serial ausgeben
		if (Serial)
			Serial.print(loc_buf);

		// 2. An das Interface senden (z.B. Telnet)
		if (external_output != NULL)
		{
			external_output(loc_buf);
		}

		// 3. In den RAM-Ringpuffer kopieren
		if (len > 0 && len < RAM_LOG_SIZE)
		{
			int space_at_end = RAM_LOG_SIZE - ram_log_idx;
			if (len <= space_at_end)
			{
				// Passt am Stück rein
				memcpy(&ram_log_buffer[ram_log_idx], loc_buf, len);
				ram_log_idx = (ram_log_idx + len) % RAM_LOG_SIZE;
			}
			else
			{
				// Muss geteilt werden: Teil 1 ans Ende, Teil 2 an den Anfang
				memcpy(&ram_log_buffer[ram_log_idx], loc_buf, space_at_end);
				memcpy(&ram_log_buffer[0], &loc_buf[space_at_end], len - space_at_end);
				ram_log_idx = len - space_at_end;
			}
			ram_log_buffer[ram_log_idx] = '\0';
		}
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
		//Serial.println("--- ALTE LOGS GEFUNDEN (PRE-RESET) ---");
		dumpRamLog();
		clearRamLog();
	}
}

#pragma GCC pop_options
