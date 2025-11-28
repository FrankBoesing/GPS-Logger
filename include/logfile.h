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
    const char *name() { return f.name(); }
    const char *path() { return f.path(); }
    int available() { return f.available(); };
    operator bool() const { return f; }
};

class cFileWrite : public cFile
{
protected:
    GPSPoint writeCache[FILECACHE_MAXPOINTS];
    size_t pointsInFileCache = 0;
    size_t pointsWritten = 0;

    virtual void flush();

public:
    using cFile::cFile;
    void open();
    void writePoint(const GPSPoint *p);
    void close();
    bool isActive(const char *path) { return (f && strcmp(f.path(), path) == 0); }
    size_t getPoints() { return pointsWritten; }
};

class cFileRead : public cFile
{
protected:
    size_t pointsRead;

public:
    using cFile::cFile;
    cFileRead(const char *filename)
    {
        f = LittleFS.open(filename, FILE_READ);
        pointsRead = 0;
    };
    bool readPoint(GPSPoint *p);
    size_t getPoints() { return pointsRead; }
};

class cCompression
{
public:
    void writeVarUint(File &f, uint32_t v);
    bool readVarUint(File &f, uint32_t &out);
    uint32_t zigzagEncode(int32_t x);
    int32_t zigzagDecode(uint32_t v);
};

class cPackedFileWrite : public cCompression, public cFileWrite
{
private:
    int32_t lastLat, lastLon;
    uint32_t lastT;

protected:
    void flush() override;

public:
    using cFileWrite::cFileWrite;
};

class cPackedFileRead : public cCompression, public cFileRead
{
private:
    int32_t lastLat, lastLon;
    uint32_t lastT;
    bool readAbsolute(GPSPoint *p);

public:
    using cFileRead::cFileRead;
    bool readPoint(GPSPoint *p);
};

#if COMPRESSION_ZIGZAG_VARINT
typedef cPackedFileWrite logfileW;
typedef cPackedFileRead logfileR;
#else
typedef cFileWrite logfileW;
typedef cFileRead logfileR;
#endif

extern logfileW logfile;

#endif
