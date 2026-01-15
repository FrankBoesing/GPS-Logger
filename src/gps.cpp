#include "gps.h"
#include "gps_hw.h"
#include "config.h"

typedef enum : uint8_t
{
	r_OK = 0,
	r_FIX,
	r_NUM_SATS,
	r_MIN_HDOP,
	r_ACCURACY,
	r_JUMPFILTER,
	r_COURSE,
	r_HDOP_SLOW,
	r_HDOP_MED,
	r_HDOP_FAST,
	r_COUNT
} gps_invalid_reason_t;

static const char *_qreason[gps_invalid_reason_t::r_COUNT] = {"", "NO FIX", "CNT SATS", "MIN HDOP", "ACCURACY", "JUMPFILTER", "COURSE", "HDOP SLOW", "HDOP MED", "HDOP FAST"};
static const char *_state[GPS_STATE_COUNT] = {"INIT", "ACQUIRE", "LOCKED", "DEGRADED", "LOST"};

typedef struct
{
	gps_invalid_reason_t reason; // Ablehnungsgrund für !valid
	bool valid;					 // darf geloggt werden?
	float accuracy_m;			 // geschätzte horizontale Genauigkeit
	float dist;					 // berechnete Entfernung
} gps_eval_t;

/****************************************/

constexpr float EARTH_RADIUS_M_F = 6371000.0f;
constexpr float DEG2RAD_F = 0.01745329251994329577f;

/* Equirectangular Approximation (Pythagoras auf einer Ebene)
	ist bis einige km Distanz völlig ausreichend exakt.
*/
static float distance_m(float lat1, float lon1, float lat2, float lon2)
{
    // Umrechnung von Grad in Bogenmaß (Radiant)
    float lat1_rad = lat1 * DEG2RAD_F;
    float lat2_rad = lat2 * DEG2RAD_F;
    float dLon_rad = (lon2 - lon1) * DEG2RAD_F;

    // Equirectangular Approximation
    // x = Differenz Längengrad korrigiert um den Breitengrad
    // y = Differenz Breitengrad
    float x = dLon_rad * cosf((lat1_rad + lat2_rad) * 0.5f);
    float y = lat2_rad - lat1_rad;

    // Distanz = Erdradius * Wurzel(x² + y²)
    return sqrtf(x * x + y * y) * EARTH_RADIUS_M_F;
}

/****************************************/

static gps_eval_t gps_evaluate_fix(
	uint fixType,
	uint numSV,			  // verwendete Satelliten
	float hdop,			  // Horizontal DOP
	float kmh,			  // km/h
	float course_deg,	  // Kurs [0..360]
	double lat, 		 // aktuelle Position
	double lon,
	double last_lat,
	double last_lon,
	float last_course,
	float dt_s // Zeit seit letztem Fix [s]
)
{
	static bool initialized = false;
	gps_eval_t out = {};
	float dist = 0, maxDist = 0, dCourse = 0;

	auto err = [&](gps_invalid_reason_t r)
	{
		switch (r)
		{
		case r_FIX ... r_MIN_HDOP:
			log_w("Eval: %s", _qreason[r]);
			break;
		case r_ACCURACY:
			log_w("Eval: Accuracy %dm", (double)out.accuracy_m);
			break;
		case r_JUMPFILTER:
			log_w("Eval: Acc: %dm Spungfilter kmh: %.1f dist: %.1fm (max:%1.fm)", lround(out.accuracy_m), (double)kmh, (double)dist, (double)maxDist);
			break;
		case r_COURSE:
			log_w("Eval: Acc: %dm Kurs %d°", lround(out.accuracy_m), lround(dCourse));
			break;
		case r_HDOP_SLOW ... r_HDOP_FAST:
			log_w("Eval: Acc: %dm, SPEED → HDOP TOO BAD, %.1f kmh, hdop: %.1f", lround(out.accuracy_m), (double)kmh, (double)hdop);
			break;
		default:
			break;
		}
		out.reason = r;
		out.valid = false;
		return out;
	};

	if (!initialized) {
		initialized = true;
		out.valid = (fixType >= 2);
		return out; //init
	}

	/* ==============================
	   Harte Ausschlusskriterien
	   ============================== */

	if (false && fixType < 2) // kein 3D-Fix
		return err(r_FIX);

	if (numSV < GPS_MIN_SATELLITES) // zu wenig Satelliten
		return err(r_NUM_SATS);

	if (hdop > (float)GPS_MIN_HDOP) // schlechte Geometrie
		return err(r_MIN_HDOP);

	/* ==============================
	   HDOP → Genauigkeit
	   ============================== */

	// Positionsfehler ≈ DOP × Range Error
	out.accuracy_m = hdop * (float)GPS_UERE;

	/* ==============================
	   Bewegungs-Sprungfilter
	   ============================== */

	dist = distance_m(lat, lon, last_lat, last_lon);
	out.dist = dist;
	// Maximal plausible Strecke: Meter/Sec * dt = Strecke.
	// Die Geschwindigkeit wird im Epfänger durch Doppler-Messung der Trägerfrequenz berechnet, nicht aus den Positionsdaten.
	// Daher kann die Geschwindigkeit hier zur Plausibilitätsprüfung verwendet werden.
	maxDist = kmh * dt_s * (1.5f / 3.6f) + 8.0f; // 1.5f → Sicherheitsfaktor (50%), /3.6 : km/h → ms/s), 8.0f: Offset(Meter), Grundrauschen GPS

	if (dist > maxDist)
		return err(r_JUMPFILTER);

	/* ==============================
	   Kurs-Konsistenzfilter
	   ============================== */

	// Kurs nur prüfen und aktualisieren, wenn wir uns wirklich bewegen (> 7 km/h)
    if (kmh > 7.0f) {
        dCourse = fabsf(course_deg - last_course);
        if (dCourse > 180.0f) dCourse = 360.0f - dCourse;

        // Wenn wir schnell sind (> 20 km/h), darf der Kurs nicht um > 45° springen
        if (kmh > 20.0f && dCourse > 45.0f) return err(r_COURSE);
    }

#if 0
	/* ==============================
	   HDOP abhängig von Geschwindigkeit
	   - die Werte hier müssen noch überarbeitet werden.
	   ============================== */

	if (kmh < 3.5f && hdop > 1.2f)
		return err(r_HDOP_SLOW);

	if (kmh >= 3.5f && kmh < 18.0f && hdop > 1.5f)
		return err(r_HDOP_MED);

	if (kmh >= 18.0f && hdop > 2.0f)
		return err(r_HDOP_FAST);
#endif
	/* ==============================
	   Fix akzeptiert
	   ============================== */

	out.valid = true;
	return out;
}

/****************************************/
bool gps_state_update(
    gps_state_ctx_t &ctx,
    uint fixType,
    uint numSV,
    float hdop,
    float kmh,
    float course_deg,
    double lat,
    double lon,
    float dt_s)
{
    /* ==============================
        Fix bewerten (Stateless)
       ============================== */
    gps_eval_t eval = gps_evaluate_fix(
        fixType,
        numSV,
        hdop,
        kmh,
        course_deg,
        lat, lon,
        ctx.lastLat,
        ctx.lastLon,
        ctx.lastCourse,
        dt_s);

    /* ==============================
        Letzte gültige Werte merken
       ============================== */
    bool goodFix = eval.valid;

    ctx.accuracy_m = eval.accuracy_m;
    ctx.lastLat = lat;
    ctx.lastLon = lon;
    ctx.lastCourse = course_deg;

    /* ==============================
        State-Machine (Signal)
       ============================== */
    switch (ctx.state)
    {
    /* ---------- INIT ---------- */
    case GPS_STATE_INIT:
        if (goodFix)
        {
            ctx.goodFixCount = 1;
            ctx.state = GPS_STATE_ACQUIRE;
        }
        break;

    /* ---------- ACQUIRE ---------- */
    case GPS_STATE_ACQUIRE:
        if (goodFix)
        {
            if (++ctx.goodFixCount >= 3)
            {
                ctx.state = GPS_STATE_LOCKED;
                ctx.badFixCount = 0;
            }
        }
        else
        {
            ctx.goodFixCount = 0;
            ctx.state = GPS_STATE_INIT;
        }
        break;

    /* ---------- LOCKED ---------- */
    case GPS_STATE_LOCKED:
        if (goodFix)
        {
            ctx.badFixCount = 0;
        }
        else
        {
            if (++ctx.badFixCount >= 2)
                ctx.state = GPS_STATE_DEGRADED;
        }
        break;

    /* ---------- DEGRADED ---------- */
    case GPS_STATE_DEGRADED:
        if (goodFix)
        {
            if (++ctx.goodFixCount >= 2)
            {
                ctx.state = GPS_STATE_LOCKED;
                ctx.badFixCount = 0;
            }
        }
        else
        {
            ctx.goodFixCount = 0;
            if (++ctx.badFixCount >= 3)
                ctx.state = GPS_STATE_LOST;
        }
        break;

    /* ---------- LOST ---------- */
    case GPS_STATE_LOST:
    default:
        if (goodFix)
        {
            ctx.goodFixCount = 1;
            ctx.badFixCount = 0;
            ctx.state = GPS_STATE_ACQUIRE;
        }
        break;
    }

    /* ==============================
        Logging-Entscheidung & Motion
       ============================== */
    bool ok = (ctx.state == GPS_STATE_LOCKED);
    ctx.mayFlush = false;

    if (ok)
    {
        gps_motion_state_t lastMotionState = ctx.motion_state;

        /* --- Bewegungserkennung (Motion State) --- */
        if (ctx.motion_state == GPS_STOPPED)
        {
            if (kmh > SPEED_MOVE_KMH && eval.dist > DIST_MOVE_M)
            {
                if (++ctx.moveCount >= MOVE_CONFIRM_CNT)
                {
                    ctx.motion_state = GPS_MOVING;
                    ctx.moveCount = ctx.stopCount = 0;
                }
            }
            else ctx.moveCount = 0;
        }
        else // MOVING
        {
            if (kmh < SPEED_STOP_KMH && eval.dist < DIST_STOP_M)
            {
                if (++ctx.stopCount >= STOP_CONFIRM_CNT)
                {
                    ctx.motion_state = GPS_STOPPED;
                    ctx.moveCount = ctx.stopCount = 0;
                }
            }
            else ctx.stopCount = 0;
        }

        // Flush bei Stop (Übergang MOVING -> STOPPED)
        ctx.mayFlush = (ctx.motion_state == GPS_STOPPED) && (lastMotionState == GPS_MOVING);
    }

    log_d("%s %dm Q: %s, B: %s",
          eval.valid ? "Gültig" : "Ungültig",
          lround(ctx.accuracy_m),
          _state[ctx.state],
          ctx.motion_state == GPS_STOPPED ? "PAUSE" : "FAHREN");

    return (ok);
}
