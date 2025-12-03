#ifndef DEBUG_H
#define DEBUG_H

#include "utils.h"

void logGPSInfo(const GPSInfo &p);

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
