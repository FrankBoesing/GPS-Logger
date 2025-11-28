#include "utils.h"
#include "logfile.h"

logfileW logfile;
SemaphoreHandle_t semFile;

static size_t _fsTotalBytes;
const size_t &fsTotalBytes = _fsTotalBytes; // make it read-only

/****************************************************************************************************************************/
/****************************************************************************************************************************/

void initGPSfile()
{
    semFile = xSemaphoreCreateBinary();
    xSemaphoreGive(semFile);

    if (!LittleFS.begin(false))
        error("LittleFS Fehler!");

    _fsTotalBytes = LittleFS.totalBytes();
}

/****************************************************************************************************************************/
/****************************************************************************************************************************/
/****************************************************************************************************************************/

// Eine Klasse ist hier am praktischten

void cFileWrite::open()
{
    if (f)
        return;

    time_t now;
    time(&now);

    bool append = false;
    char filename[32];

    // Falls in LogMode eingeschaltet, prüfen ob an die jüngste Datei angehängt werden soll:
    if (RESTART_AFTER_IDLE) // An letztes Logfile anfügen?
    {
        time_t lastWritten;
        bool found = findFile(true, filename, sizeof(filename), &lastWritten);
        if (found && (now - lastWritten <= MAX_IDLE_SECONDS))
        {
            append = true;
            log_i("Füge an Logfile an. Alter: %llds", now - lastWritten);
        }
    }

    if (!append)
    {
        // use time as filename
        snprintf(filename, sizeof(filename), FILE_PREFIX "%lld" FILE_SUFFIX, now);
    }

    pointsWritten = pointsInFileCache = 0;
    xSemaphoreTake(semFile, portMAX_DELAY);
    f = LittleFS.open(filename, append ? FILE_APPEND : FILE_WRITE);
    // f.setBufferSize(sizeof(writeCache)); // Sichergehen, dass die Punkte in den File-Cache passen
    xSemaphoreGive(semFile);
    log_i("Logfile: %s", filename);
}

void cFileWrite::close()
{
    if (!f)
        return;

    xSemaphoreTake(semFile, portMAX_DELAY);
    flush();
    f.close();
    xSemaphoreGive(semFile);
    pointsInFileCache = 0;
    log_v("Datei geschlossen");
}

// Flush all Points
void cFileWrite::flush()
{
    if (pointsInFileCache > 0)
        f.write((const uint8_t *)writeCache, sizeof(GPSPoint) * pointsInFileCache);
    pointsWritten += pointsInFileCache;
    f.flush();
}

void cFileWrite::writePoint(const GPSPoint *p)
{
    if (!f)
    {
        log_e("Kann nicht loggen. Logfile geschlossen");
    }
    else
    {
        writeCache[pointsInFileCache++] = *p;
        if (pointsInFileCache >= FILECACHE_MAXPOINTS)
        {
            flush();
            pointsInFileCache = 0;
            log_d("Cache flushed");
        }
    }
}

/****************************************************************************************************************************/

bool cFileRead::readPoint(GPSPoint *p)
{
    int len = f.readBytes((char *)p, sizeof(GPSPoint));
    if (len == sizeof(GPSPoint))
    {
        pointsRead++;
        return true;
    }
    return false;
}

/****************************************************************************************************************************/
/****************************************************************************************************************************/
// varint / zigzag Komprimierung

void cCompression::writeVarUint(File &f, uint32_t v)
{
    while (v >= 0x80)
    {
        uint8_t b = (v & 0x7F) | 0x80;
        f.write(b);
        v >>= 7;
    }
    f.write((uint8_t)v);
}

bool cCompression::readVarUint(File &f, uint32_t &out)
{
    uint32_t result = 0;
    uint8_t shift = 0;
    int tries = 0;
    while (true)
    {
        int r = f.read();
        if (r < 0)
            return false; // EOF or error
        uint8_t b = (uint8_t)r;
        result |= (uint32_t)(b & 0x7F) << shift;
        if (!(b & 0x80))
            break;
        shift += 7;
        if (++tries > 5)
            break; // safety (uint32 needs max 5 bytes)
    }
    out = result;
    return true;
}

uint32_t cCompression::zigzagEncode(int32_t x)
{
    return (uint32_t)((uint32_t)(x << 1) ^ (uint32_t)(x >> 31));
}

int32_t cCompression::zigzagDecode(uint32_t v)
{
    return (int32_t)((v >> 1) ^ (-(int32_t)(v & 1)));
}

static constexpr const double SCALE = 1e6; // 1e5 -> ~1.11 m resolution
static constexpr const size_t SZ32 = sizeof(int32_t);
static constexpr const uint8_t ESCAPE[3] = {0x7f, 0x7f, 0x7f};

// Flush all Points
void cPackedFileWrite::flush()
{

    int p = 0;
    bool append = pointsWritten == 0 && f.size() > 0;

    if (append)
    { // Es wird an eine vorhandene Datei angefügt. Escape schreiben.
        f.write(ESCAPE, 3);
        log_d("Escape geschrieben");
    }

    if (pointsWritten == 0)
    { // 1. Punkt - unkomprimiert:
        lastLat = (int32_t)llround(writeCache[0].lat * SCALE);
        lastLon = (int32_t)llround(writeCache[0].lon * SCALE);
        lastT = writeCache[0].time;

        f.write((const uint8_t *)&lastT, SZ32);
        f.write((const uint8_t *)&lastLat, SZ32);
        f.write((const uint8_t *)&lastLon, SZ32);

        pointsWritten = 1;
        p = 1;
    }

    // Deltas
    for (; p < pointsInFileCache; ++p)
    {
        int32_t latSi = (int32_t)llround(writeCache[p].lat * SCALE);
        int32_t lonSi = (int32_t)llround(writeCache[p].lon * SCALE);
        uint32_t ti = writeCache[p].time;

        int32_t dLat = latSi - lastLat;
        int32_t dLon = lonSi - lastLon;
        uint32_t dT = (ti >= lastT) ? (ti - lastT) : 0; // falls Uhrenrücksetzung -> 0 (alternativ: abs or escape)

        lastLat = latSi;
        lastLon = lonSi;
        lastT = ti;

        writeVarUint(f, dT);
        writeVarUint(f, zigzagEncode(dLat));
        writeVarUint(f, zigzagEncode(dLon));

        pointsWritten++;
    }

    f.flush();
    log_d("Points written: %d", pointsWritten);
}

bool cPackedFileRead::readAbsolute(GPSPoint *p)
{
    uint32_t latS_u, lonS_u, t_u;

    if (!f.read((uint8_t *)&t_u, SZ32))
        return false;
    if (!f.read((uint8_t *)&latS_u, SZ32))
        return false;
    if (!f.read((uint8_t *)&lonS_u, SZ32))
        return false;

    lastLat = (int32_t)latS_u;
    lastLon = (int32_t)lonS_u;
    lastT = t_u;

    p->lat = ((double)lastLat) / SCALE;
    p->lon = ((double)lastLon) / SCALE;
    p->time = lastT;
    return true;
}

bool cPackedFileRead::readPoint(GPSPoint *p)
{
    if (!f)
        return false;

    if (pointsRead == 0)
    {
        pointsRead = 1;
        return readAbsolute(p);
    }

    // Deltas

    uint32_t vLat, vLon, vT;
    if (!readVarUint(f, vT))
        return false;
    if (!readVarUint(f, vLat))
        return false;
    if (!readVarUint(f, vLon))
        return false;

    if (vT == ESCAPE[0] && vLat == ESCAPE[1] && vLon == ESCAPE[2])
    { // Absoluten Datensatz lesen.
        log_d("Escape gefunden");
        pointsRead++;
        return readAbsolute(p);
    }

    uint32_t dT = vT;
    int32_t dLat = zigzagDecode(vLat);
    int32_t dLon = zigzagDecode(vLon);

    lastT += dT;
    lastLat += dLat;
    lastLon += dLon;

    p->lat = ((double)lastLat) / SCALE;
    p->lon = ((double)lastLon) / SCALE;
    p->time = lastT;

    pointsRead++;
    return true;
}
