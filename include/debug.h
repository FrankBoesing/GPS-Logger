#ifndef DEBUG_H
#define DEBUG_H

#include "config.h"


#define PRINTF_BUFFER 256

void printSerialData(const char ch);
void logGPSInfo(const unsigned long loopmillis);

//Print to both, serial and telnet:
int write(const char *buf, const size_t len);
// int print(String& s); //Print to both, serial and telnet
// int printf(const char *fmt, va_list args) __attribute__((format(printf, 1, 0)));

#define LEDON()             \
  {                         \
    digitalWrite(LED, LOW); \
  }
#define LEDOFF()             \
  {                          \
    digitalWrite(LED, HIGH); \
  }
#define TOGGLELED()                       \
  {                                       \
    digitalWrite(LED, !digitalRead(LED)); \
  }

#endif
