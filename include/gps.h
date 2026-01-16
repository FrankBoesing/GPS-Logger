#ifndef GPS_H
#define GPS_H
#include <Arduino.h>

#define SPEED_MOVE_KMH 3.0f
#define SPEED_STOP_KMH 1.0f

#define DIST_MOVE_M 2.0f // Bewegung sichtbar (m)
#define DIST_STOP_M 0.8f // Driftgrenze (m)

#define MOVE_CONFIRM_CNT 1 // Sekunden
#define STOP_CONFIRM_CNT 2 // Sekunden

typedef enum : uint8_t
{
	GPS_STOPPED = 0,
	GPS_MOVING
} gps_motion_state_t;

typedef enum : uint8_t
{
	GPS_STATE_INIT = 0, // INIT     → warten auf ersten Fix
	GPS_STATE_ACQUIRE,	// ACQUIRE  → Fix da, aber noch instabil
	GPS_STATE_LOCKED,	// LOCKED   → stabil, loggen erlaubt
	GPS_STATE_DEGRADED, // DEGRADED → Qualität sinkt, nicht loggen
	GPS_STATE_LOST,		// LOST     → Fix verloren
	GPS_STATE_COUNT
} gps_state_t;

typedef struct
{
	double lat, lng;
	float hdop, kmh, course;
	float dt_gps;
	uint8_t satellites;
	uint8_t fix;
} gps_data_t;
typedef struct
{
	uint goodFixCount; // aufeinanderfolgende gute Fixes
	uint badFixCount;  // aufeinanderfolgende schlechte Fixes
	uint stopCount;
	uint moveCount;
	float accuracy_m;
	float lastCourse;
	float lastLat, lastLon;
	gps_state_t state;
	gps_motion_state_t motion_state;
	bool mayFlush;
} gps_state_ctx_t;

/****************************************/

bool gps_state_update(const gps_data_t &data, gps_state_ctx_t &ctx);

#endif
