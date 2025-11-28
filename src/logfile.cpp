#include "utils.h"
#include "logfile.h"

cFileWrite logfile;
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

    points = pointsInFileCache = 0;
    xSemaphoreTake(semFile, portMAX_DELAY);
    f = LittleFS.open(filename, append ? FILE_APPEND : FILE_WRITE);
    f.setBufferSize(sizeof(writeCache)); // Sichergehen, dass die Punkte in den File-Cache passen
    xSemaphoreGive(semFile);
    log_i("Logfile: %s", filename);
}

void cFileWrite::close()
{
    if (!f)
        return;

    xSemaphoreTake(semFile, portMAX_DELAY);
    f.flush();
    f.close();
    xSemaphoreGive(semFile);
    pointsInFileCache = 0;
    log_v("Datei geschlossen");
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
        points++;
        if (pointsInFileCache >= FILECACHE_MAXPOINTS)
        {
            xSemaphoreTake(semFile, portMAX_DELAY);
            f.write((const uint8_t *)writeCache, sizeof(writeCache));
            f.flush();
            xSemaphoreGive(semFile);
            pointsInFileCache = 0;
            log_d("Cache flushed");
        }
    }
}

/****************************************************************************************************************************/

bool cFileRead::readPoint(const GPSPoint *p)
{
    int len = f.readBytes((char *)p, sizeof(GPSPoint));
    return len == sizeof(GPSPoint);
}

/****************************************************************************************************************************/
/****************************************************************************************************************************/
// varint / zigzag Komprimierung

/*
bool cCompressedFileRead::readPoint(const GPSPoint *p)
{
    return false;
}
    */
