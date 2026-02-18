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

typedef struct {
    float accuracy_m;
    float dist_m;

    float confidence;   // 0..1
    float q_hdop;
    float q_jump;
    float q_course;
    float q_sats;

    bool  valid;
	gps_invalid_reason_t reason;

} gps_eval_t;

static constexpr const char *_qreason[gps_invalid_reason_t::r_COUNT] = {"", "NO FIX", "CNT SATS", "MIN HDOP", "ACCURACY", "JUMPFILTER", "COURSE", "HDOP SLOW", "HDOP MED", "HDOP FAST"};
static constexpr const char *_state[GPS_STATE_COUNT] = {"INIT", "ACQUIRE", "LOCK", "DEGRADED", "LOST"};

/****************************************/

const constexpr float EARTH_RADIUS_M_F = 6371000.0f;
const constexpr float DEG2RAD_F = 0.01745329251994329577f;

/* Equirectangular Approximation (Pythagoras auf einer Ebene)
	ist bis einige km Distanz völlig ausreichend exakt.
	fast_cosf() ist völlig ausreichend. Wird hier genutzt um Flash zu sparen, nicht für Geschwindigkeit.
*/
static inline float fast_cosf(float x)
{
    float x2 = x * x;
    return 1.0f + x2 * (-0.5f + x2 * (1.0f / 24.0f));
}

static float distance_m(float lat1, float lon1, float lat2, float lon2)
{
    float latSumRad = (lat1 + lat2) * (DEG2RAD_F * 0.5f);
    float dx = (lon2 - lon1) * DEG2RAD_F * fast_cosf(latSumRad);
    float dy = (lat2 - lat1) * DEG2RAD_F;

    return sqrtf(dx * dx + dy * dy) * EARTH_RADIUS_M_F;
}

/****************************************/

static gps_eval_t gps_evaluate_fix(const gps_data_t &data,
                                  const gps_state_ctx_t &ctx)
{
    gps_eval_t out = {};
    float maxDist, dCourse, jump_ratio;

    /* ==============================
       HDOP → Genauigkeit
       ============================== */

    out.accuracy_m = data.hdop * GPS_UERE;

    /* ==============================
       Harte Ausschlusskriterien
       ============================== */

    if (data.satellites < 4) {
        out.reason = r_NUM_SATS;
        goto reject;
    }

    /* ==============================
       Bewegungs-Sprungfilter
       ============================== */

    out.dist_m = distance_m(data.lat, data.lng, ctx.lastLat, ctx.lastLon);
    maxDist = data.kmh * data.dt_gps * (1.5f / 3.6f) + 8.0f;

    if (out.dist_m > maxDist) {
        out.reason = r_JUMPFILTER;
        goto reject;
    }

    /* ==============================
       Kurs (harte Extremgrenze)
       ============================== */

    if (data.kmh > 7.0f) {
        dCourse = fabsf(data.course - ctx.lastCourse);
        if (dCourse > 180.0f)
            dCourse = 360.0f - dCourse;

        if (data.kmh > 20.0f && dCourse > 90.0f) {
            out.reason = r_COURSE;
            goto reject;
        }
    } else dCourse = 0.0f;

    /* ==============================
       Weiche Qualitätsmetriken
       ============================== */

    /* --- HDOP --- */
    if (data.hdop <= 1.2f)
        out.q_hdop = 1.0f;
    else if (data.hdop >= (float)GPS_MIN_HDOP)
        out.q_hdop = 0.0f;
    else
        out.q_hdop = 1.0f - (data.hdop - 1.2f) * (1.0f / ((float)GPS_MIN_HDOP - 1.2f));

    /* --- Jump --- */
    jump_ratio = out.dist_m / maxDist;

    if (jump_ratio <= 0.6f)
        out.q_jump = 1.0f;
    else if (jump_ratio >= 1.0f)
        out.q_jump = 0.0f;
    else
        out.q_jump = 1.0f - (jump_ratio - 0.6f) * (1.0f / 0.4f);

    /* --- Course --- */
    if (data.kmh < 7.0f)
        out.q_course = 1.0f;
    else if (dCourse <= 10.0f)
        out.q_course = 1.0f;
    else if (dCourse >= 60.0f)
        out.q_course = 0.0f;
    else
        out.q_course = 1.0f - (dCourse - 10.0f) * (1.0f / 50.0f);

    /* --- Satelliten --- */
    if (data.satellites >= 10)
        out.q_sats = 1.0f;
    else if (data.satellites <= (int)GPS_MIN_SATELLITES)
        out.q_sats = 0.0f;
    else
        out.q_sats = (data.satellites - (int)GPS_MIN_SATELLITES) * 0.2f;

    /* ==============================
       Confidence
       ============================== */

    out.confidence =
        0.40f * out.q_jump +
        0.25f * out.q_course +
        0.20f * out.q_hdop +
        0.15f * out.q_sats;

    /* ==============================
       Logging
       ============================== */

    log_d("GPS Eval: conf=%.2f hdop=%.2f(q=%.2f) jump=%.2f(q=%.2f) course=%.1f(q=%.2f) sats=%d(q=%.2f)",
          (double)out.confidence,
          (double)data.hdop, (double)out.q_hdop,
          (double)jump_ratio, (double)out.q_jump,
          (double)dCourse, (double)out.q_course,
          data.satellites, (double)out.q_sats);

    /* ==============================
       Entscheidung
       ============================== */

    if (out.confidence >= 0.75f) {
        out.valid  = true;
        out.reason = r_OK;
        return out;
    }

    if (out.confidence >= 0.45f) {
        out.valid  = true;
        out.reason = r_ACCURACY;   // „weich akzeptiert“
        return out;
    }

    out.reason = r_ACCURACY;

reject:
    log_w("GPS Reject: %s conf=%.2f hdop=%.2f dist=%.1f",
          _qreason[out.reason],
          (double)out.confidence,
          (double)data.hdop,
          (double)out.dist_m);

    out.valid  = false;
    return out;
}

/****************************************/
bool gps_state_update(const gps_data_t &data, gps_state_ctx_t &ctx)
{
    /* ==============================
        Fix bewerten (Stateless)
       ============================== */
    const gps_eval_t eval = gps_evaluate_fix( data, ctx );

    /* ==============================
        Letzte gültige Werte merken
       ============================== */
    const bool goodFix = eval.valid;

    ctx.accuracy_m = eval.accuracy_m;
    ctx.lastLat = data.lat;
    ctx.lastLon = data.lng;
    ctx.lastCourse = data.course;

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
    const bool ok = (ctx.state == GPS_STATE_LOCKED || ctx.state == GPS_STATE_DEGRADED);
    ctx.mayFlush = false;

    if (ok)
    {
        const gps_motion_state_t lastMotionState = ctx.motion_state;

        /* --- Bewegungserkennung (Motion State) --- */
        if (ctx.motion_state == GPS_STOPPED)
        {
            if (data.kmh > SPEED_MOVE_KMH &&
				eval.dist_m > DIST_MOVE_M &&
				eval.accuracy_m < 10.0f) //<10m
            {
                if (++ctx.moveCount >= MOVE_CONFIRM_CNT)
                {
                    ctx.motion_state = GPS_MOVING;
                    ctx.moveCount = ctx.stopCount = 0;
                }
            }
            else ctx.moveCount = 0;
        }
        else  [[likely]] // MOVING
        {
            if (data.kmh < SPEED_STOP_KMH &&
				eval.dist_m < DIST_STOP_M &&
				eval.accuracy_m < 10.0f) //<10m
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

    log_i("%s %dm Q: %s, B: %s",
          eval.valid ? "Gültig" : "Ungültig",
          lround(ctx.accuracy_m),
          _state[ctx.state],
          ctx.motion_state == GPS_STOPPED ? "PAUSE" : "FAHREN");

    return (ok);
}
