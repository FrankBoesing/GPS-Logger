#pragma once

#include "includes.h"

#define FILE_PREFIX "/"
#define FILE_SUFFIX ".vic"
#define MAX_FILES 100u

constexpr size_t LEN_FILENAME = 1 +
								(sizeof(FILE_PREFIX) - 1) +
								std::numeric_limits<long long>::digits10 + 1 +
								(sizeof(FILE_SUFFIX) - 1);

struct file_info_t
{
	time_t id;
	time_t lastWrite;
	bool active;
	auto operator<=>(const file_info_t &o) const { return id <=> o.id; }
	bool operator==(const file_info_t &o) const { return id == o.id; }
};

extern SortedStaticArray<file_info_t, MAX_FILES> filelist;

void initLogfile();

struct GPSPoint_t
{
	double lat;
	double lon;
	time_t time;
};

class logfileW
{
public:
	void open(const time_t time); // time wird zur Erzeugung des Dateinamens genutzt
	void writePoint(const double lat, const double lon, const time_t time, const bool forceFlush);
	void close();
	bool isActive(const char *path) { return (f && strcmp(f.path(), path) == 0); }
	size_t getPoints() { return pointsWritten; }
	void intervalFlush();
	operator bool() const { return f; }

protected:
	GPSPoint_t writeCache[FILECACHE_MAXPOINTS];
	size_t pointsInFileCache = 0;
	size_t pointsWritten = 0;
	File f;

private:
	int32_t lastLat, lastLon;
	uint32_t lastT;
	ulong lastFlush;
	void flush();
	void _flush_unlocked();
	void writeVarUint(uint32_t v);
	inline uint32_t zigzagEncode(int32_t x);
};

class logfileR
{
public:
	bool open(const char *filename)
	{
		f.close();
		f = LittleFS.open(filename, FILE_READ);
		pointsRead = 0;
        // Reset read buffer
        readBufPos = readBufLen = 0;
		return (f && !f.isDirectory());
	}
	void close() { f.close(); readBufPos = readBufLen = 0; }
	bool readPoint(GPSPoint_t &p);

	operator bool() const { return f; }

protected:
	size_t pointsRead;
	File f;

private:

	uint32_t lastT;
	int32_t lastLat, lastLon;

	bool readVarUint(uint32_t &out);
	inline int32_t zigzagDecode(uint32_t v);

	// Buffered read support
	static constexpr size_t READBUF_SIZE = 256;
	uint8_t readBuf[READBUF_SIZE];
	size_t readBufPos = 0;
	size_t readBufLen = 0;

	inline bool refillBuffer();
	int bufferedRead();
	size_t bufferedRead(uint8_t *dst, size_t len);
};
