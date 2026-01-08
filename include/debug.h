#ifndef DEBUG_H
#define DEBUG_H

#include <Arduino.h>

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
