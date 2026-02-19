#include "includes.h"

SortedStaticArray<file_info_t, MAX_FILES> filelist;

logfileW logfile;
SemaphoreHandle_t logfile_sem;

/****************************************************************************************************************************/
/****************************************************************************************************************************/

// Aufrufen in setup()
void initLogfile()
{
	logfile_sem = xSemaphoreCreateBinary();
	xSemaphoreGive(logfile_sem);
}

/****************************************************************************************************************************/

static const constexpr long SCALE = 10000000L;
static const constexpr double RSCALE = 1.0 / SCALE;
static const constexpr size_t SZ32 = sizeof(int32_t);
static const constexpr uint32_t MAGIC = 0xbabeef;

// Eine Klasse ist hier am praktischten:

void logfileW::open(const time_t time)
{
	if (f)
		return;

	bool append = false;
	char filename[LEN_FILENAME];

	// Falls in LogMode eingeschaltet, prüfen ob an die jüngste Datei angehängt werden soll:
	if (logAppend && filelist.size() > 0) // An letztes Logfile anfügen?
	{
		const auto& e = *(filelist.end() - 1);
		if (time - e.lastWrite <= MAX_IDLE_SECONDS)
		{
			append = true;
			id2filename(e.id, filename, sizeof(filename));
			logi("Füge an Logfile an. Alter: %llds", time - e.lastWrite);
		}
	}

	if (!append)
		id2filename(time, filename, sizeof(filename));

	xSemaphoreTake(logfile_sem, portMAX_DELAY);
	pointsWritten = pointsInFileCache = 0;
	f = LittleFS.open(filename, append ? FILE_APPEND : FILE_WRITE);
	filelistSetActive(f, true);
	xSemaphoreGive(logfile_sem);

	uiSendJson(); // UI über neue Datei benachrichtigen
	logi("Logfile: %s", filename);
}

void logfileW::close() {
	if (!f) return;
    xSemaphoreTake(logfile_sem, portMAX_DELAY);

	if (pointsInFileCache > 0) {
		_flush_unlocked(); // Letzte Daten sichern
	}
	filelistSetActive(f, false);
	f.close();
	pointsInFileCache = 0; // Cache für die nächste Tour leeren
	pointsWritten = 0;     // Status zurücksetzen

    xSemaphoreGive(logfile_sem);
	uiSendJson();
}

void logfileW::writePoint(const double lat, const double lon, const time_t time, const bool forceFlush)
{
    if (!f) return;

    xSemaphoreTake(logfile_sem, portMAX_DELAY);

    if (pointsInFileCache >= FILECACHE_MAXPOINTS) {
        _flush_unlocked();
        pointsInFileCache = 0;
    }

    writeCache[pointsInFileCache++] = {lat, lon, time};

    if (forceFlush || pointsWritten == 0)
    {
        _flush_unlocked();
        pointsInFileCache = 0;
    }

    xSemaphoreGive(logfile_sem);
}

void logfileW::writeVarUint(uint32_t v)
{
	while (v >= 0x80)
	{
		uint8_t b = (v & 0x7F) | 0x80;
		f.write(b);
		v >>= 7;
	}
	f.write((uint8_t)v);
}

uint32_t logfileW::zigzagEncode(int32_t x)
{
	return (uint32_t)((uint32_t)(x << 1) ^ (uint32_t)(x >> 31));
}

void  logfileW::_flush_unlocked() {
    if (pointsInFileCache == 0 || !f) return;

	lastFlush = micros();
    size_t p = 0;

    if (pointsWritten == 0) {
        if (f.size() > 0) {
            writeVarUint(MAGIC); // Anfüge-Marker
        }

        // Erster Punkt unkomprimiert (immer double -> int32)
        lastLat = (int32_t)lround(writeCache[0].lat * SCALE);
        lastLon = (int32_t)lround(writeCache[0].lon * SCALE);
        lastT   = (uint32_t)(writeCache[0].time);

        f.write((const uint8_t *)&lastT, SZ32);
        f.write((const uint8_t *)&lastLat, SZ32);
        f.write((const uint8_t *)&lastLon, SZ32);
        p = 1;
    }

    for (; p < pointsInFileCache; ++p) {
        int32_t latSi = (int32_t)lround(writeCache[p].lat * SCALE);
        int32_t lonSi = (int32_t)lround(writeCache[p].lon * SCALE);
        uint32_t ti   = (uint32_t)(writeCache[p].time);

        int32_t dLat = latSi - lastLat;
        int32_t dLon = lonSi - lastLon;

        // dT Schutz gegen Zeit-Sprünge (z.B. GPS-Fix Korrektur nach hinten)
        uint32_t dT = (ti >= lastT) ? (ti - lastT) : 0;

        lastLat = latSi;
        lastLon = lonSi;
        lastT = ti;

        writeVarUint(dT);
        writeVarUint(zigzagEncode(dLat));
        writeVarUint(zigzagEncode(dLon));
    }

    f.flush();

    pointsWritten += pointsInFileCache;
    pointsInFileCache = 0;

	logd("Points written: %d", pointsWritten);

}

void logfileW::flush() {
    xSemaphoreTake(logfile_sem, portMAX_DELAY);
	_flush_unlocked();
    xSemaphoreGive(logfile_sem);
}

void logfileW::periodicFlush()
{
	if (micros() - lastFlush > FILECACHE_MAXAGE * SECOND)
		flush();
}
/****************************************************************************************************************************/
#pragma GCC push_options
#pragma GCC optimize ("O2")

bool logfileR::readVarUint(uint32_t &out)
{
	uint32_t result = 0;
	uint8_t shift = 0;
	uint tries = 0;
	while (true)
	{
		int r = f.read();
		if (r < 0)
			return false; // EOF oder Fehler
		uint8_t b = (uint8_t)r;
		result |= (uint32_t)(b & 0x7F) << shift;
		if (!(b & 0x80))
			break;
		shift += 7;
		if (++tries > 5)
			return false; // safety (uint32 braucht max 5 Bytes)
	}
	out = result;
	return true;
}

inline int32_t logfileR::zigzagDecode(uint32_t v)
{
	return (int32_t)((v >> 1) ^ (-(int32_t)(v & 1)));
}

bool logfileR::readAbsolute(GPSPoint_t &p)
{
	uint32_t latS_u, lonS_u, t_u;

	f.read((uint8_t *)&t_u, SZ32);
	f.read((uint8_t *)&latS_u, SZ32);
	if (!f.read((uint8_t *)&lonS_u, SZ32))
		return false;

	lastLat = (int32_t)latS_u;
	lastLon = (int32_t)lonS_u;
	lastT = t_u;

	p.lat = lastLat * RSCALE;
	p.lon = lastLon * RSCALE;
	p.time = (time_t)lastT;
	pointsRead++;
	return true;
}

bool logfileR::readPoint(GPSPoint_t &p) {
    if (pointsRead == 0) return readAbsolute(p);

    uint32_t vT;
    if (!readVarUint(vT)) return false;

    // Falls wir an einen MAGIC Marker stoßen (append-Stelle)
    if (vT == MAGIC) return readAbsolute(p);

    uint32_t vLat, vLon;
    if (!readVarUint(vLat)) return false;
    if (!readVarUint(vLon)) return false;

    lastT   += vT;
    lastLat += zigzagDecode(vLat);
    lastLon += zigzagDecode(vLon);

    // Rückrechnung von int32_t zu double
    p.lat  = (double)lastLat * RSCALE;
    p.lon  = (double)lastLon * RSCALE;
    p.time = (time_t)lastT;

    pointsRead++;
    return true;
}

#pragma GCC pop_options

/*
	Liest Anzahl der aufgezeichneten Punkte, und Zeitpunkt des ersten und letzen Trackpoints
	Evtl irgendwann nützlich (bisher nicht benötigt)
*/
size_t logfileR::getFileInfo(GPSPoint_t &firstp, GPSPoint_t &lastp)
{
	// ulong m = micros();
	if (!f)
		return 0;
	f.seek(0);
	pointsRead = 0;

	firstp = lastp = {0};

	if (!readPoint(firstp))
		return 0;

	while (readPoint(lastp))
		;

	// logi("%s: #%u %llu, %llu (%u ms)", path, pts, firstp.time, lastp.time, (micros() - m) / 1000);
	return pointsRead;
}
