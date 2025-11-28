#ifndef LOGFILE_H
#define LOGFILE_H

#include <Arduino.h>
#include <LittleFS.h>
#include "config.h"

#define FILE_PREFIX "/"
#if COMPRESSION_ZIGZAG_VARINT
typedef double gpsfloat_t;
#define FILE_SUFFIX ".vic"
#else
typedef float gpsfloat_t;
#define FILE_SUFFIX ".bin"
#endif

extern SemaphoreHandle_t semFile;
extern const size_t &fsTotalBytes;

#define TIMEOFFSET 1761955200LL
typedef uint32_t time32_t;
struct GPSPoint
{
    gpsfloat_t lat;
    gpsfloat_t lon;
    time32_t time; // Zeit in 32 Bit speichern (von time_t wird TIMEOFFSET abgezogen)
};

void initGPSfile();

void openLogFile();
void logPoint(const GPSPoint *p);
void closeLogFile();

class cFile
{
protected:
    File f;

public:
    ~cFile() { close(f); }
    inline const char *name() { return f.name(); }
    inline const char *path() { return f.path(); }
    inline operator bool() const { return f; }
};

class cFileWrite : public cFile
{
private:
    GPSPoint writeCache[FILECACHE_MAXPOINTS];
    size_t pointsInFileCache = 0;
    size_t points = 0;

public:
    void open();
    void writePoint(const GPSPoint *p);
    void close();
    inline bool isActive(const char *path) { return (f && strcmp(f.path(), path) == 0); }
    inline size_t getPoints() { return points; }
};

class cFileRead : public cFile
{
public:
    cFileRead(const char *filename) { f = LittleFS.open(filename, FILE_READ); };
    inline int available() { return f.available(); };
    bool readPoint(const GPSPoint *p);
};

#if 0
class cCompressedFileRead : cFileRead
{
    // int available();
    // bool readPoint(const GPSPoint *p);

    -....
};
#endif

extern cFileWrite logfile;
#endif
