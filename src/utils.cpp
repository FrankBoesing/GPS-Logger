#include "includes.h"
#include <Preferences.h>

extern SemaphoreHandle_t logfile_sem;
std::vector<wifiCredentials_t> wifiCreds(WIFI_MAX_NETWORKS);

[[gnu::optimize("O2")]]
void boost(const bool fast)
{
#if !defined(ENABLE_HEAT_REDUCTION) || !ENABLE_HEAT_REDUCTION
	return;
#endif

	static uint32_t lastBoost = 0;
	static bool isFast = true;

	if (fast)
	{
		if (!isFast)
		{
			// Performance-Profil
			setCpuFrequencyMhz(CPU_FREQ_DEFAULT);
			WiFi.setSleep(false);
			isFast = true;
		}
		lastBoost = millis();

		return;
	}

	if (isFast && (millis() - lastBoost > 5000))
	{
		// Cooling-Profil
		setCpuFrequencyMhz(CPU_FREQ_IDLE);
		WiFi.setSleep(true);
		isFast = false;
	}
}

/****************************************************************************************************************************/
/****************************************************************************************************************************/

void onWiFiEvent(arduino_event_id_t event)
{
	bool triggerMDNS = false;

	switch (event)
	{
	case ARDUINO_EVENT_WIFI_STA_GOT_IP:
		logi("Verbunden! IP: %s", WiFi.localIP().toString().c_str());
		triggerMDNS = true;
		break;

	case ARDUINO_EVENT_WIFI_AP_START:
		triggerMDNS = true;
		break;

	default:
		break;
	}

	// mDNS handling
	if (triggerMDNS && MDNS.begin(HOSTNAME))
	{
		static bool servicesAdded = false;
		if (!servicesAdded)
		{
			servicesAdded = true;
			MDNS.addService("http", "tcp", 80);
		}
	}
}

/****************************************************************************************************************************/
void loadPrefs()
{
	Preferences pref;
	if (!pref.begin("gps", true))
		return;

	logMode = (log_mode_t)pref.getChar("logMode", (char)DEFAULTLOGMODE);
	logAppend = pref.getBool("logAppend", true);

	size_t count = pref.getUInt("wifi_count", 0);
	if (count > 0)
	{
		wifiCreds.resize(count);
		size_t loadedBytes = pref.getBytes("wifi_data", wifiCreds.data(), count * sizeof(wifiCredentials_t));
		if (loadedBytes != count * sizeof(wifiCredentials_t))
		{
			loge("Fehler beim Laden der WLAN-Daten");
			wifiCreds.clear();
		}
	}
	else
		wifiCreds.clear();
	pref.end();
}

void savePrefs()
{
	Preferences pref;
	if (!pref.begin("gps", false))
		return;

	pref.putChar("logMode", (char)logMode);
	pref.putBool("logAppend", logAppend);

	wifiCreds.erase( // leere Elemente entfernen
		std::remove_if(wifiCreds.begin(), wifiCreds.end(),
					   [](const auto &c)
					   { return strlen(c.ssid) == 0; }),
		wifiCreds.end());

	pref.putUInt("wifi_count", wifiCreds.size());
	pref.putBytes("wifi_data", wifiCreds.data(), wifiCreds.size() * sizeof(wifiCredentials_t));

	pref.end();
}

/****************************************************************************************************************************/
static bool endsWith(const char *str, const char *suffix)
{
	if (!str || !suffix)
		return false;
	size_t len = strlen(str);
	size_t slen = strlen(suffix);
	if (slen > len)
		return false;
	return strcmp(str + len - slen, suffix) == 0;
}

/****************************************************************************************************************************/
// Dateiliste

int id2filename(const time_t id, char *buf, const size_t szbuf)
{
	return snprintf(buf, szbuf, FILE_PREFIX "%lld" FILE_SUFFIX, id);
}

static void filelistRmv(const char *path)
{
	const time_t id = atoll(path + strlen(FILE_PREFIX));
	if (id)
		filelist.remove(file_info_t{.id = id});
}

static void filelistAdd(File &f, bool active = false)
{
	if (!f)
		return;

	file_info_t e;
	e.id = atoll(f.path() + strlen(FILE_PREFIX));
	e.lastWrite = f.getLastWrite();
	e.active = active;

	filelist.insert(e);
}

void filelistSetActive(File &f, bool active)
{
	if (!f)
		return;
	const time_t id = atoll(f.path() + strlen(FILE_PREFIX));
	const auto lastWrite = f.getLastWrite();

	if (auto *e = filelist.find(file_info_t{.id = id}); e)
	{
		e->active = active;
		e->lastWrite = lastWrite;
	}
	else
		filelistAdd(f, active);
}

size_t readFileList(const char *fileext)
{
	size_t count = 0;
	xSemaphoreTake(logfile_sem, portMAX_DELAY);
	filelist.clear();

	File root = LittleFS.open("/");
	File fnext = root.openNextFile();

	while (fnext)
	{
		const char *name = fnext.path();
		if (endsWith(name, fileext))
		{
			++count;
			bool active = logfile.isActive(name);
			filelistAdd(fnext, active);
		}
		vTaskDelay(pdMS_TO_TICKS(1));
		fnext = root.openNextFile();
	}
	root.close();
	xSemaphoreGive(logfile_sem);
	return count;
}

size_t deleteAllFiles()
{
	logi("Del all files");
	size_t count = 0;
	char path[LEN_FILENAME];
	xSemaphoreTake(logfile_sem, portMAX_DELAY);
	for (const auto &e : filelist)
	{
		if (e.active)
			continue;
		id2filename(e.id, path, sizeof(path));
		if (LittleFS.remove(path))
			++count;
		vTaskDelay(pdMS_TO_TICKS(1));
	}

	xSemaphoreGive(logfile_sem);
	readFileList();
	return count;
}

size_t deleteFile(const time_t id)
{
	size_t count = 0;
	if (!filelist.remove(file_info_t{.id = id}))
		return count;

	char filename[LEN_FILENAME];
	id2filename(id, filename, sizeof(filename));

	xSemaphoreTake(logfile_sem, portMAX_DELAY);
	if (LittleFS.remove(filename))
	{
		count = 1;
	}
	else
	{
		logw("Konnte %s nicht löschen", filename);
	}
	xSemaphoreGive(logfile_sem);
	return count;
}

/****************************************************************************************************************************/
/*
 - Löscht alle leere Logfiles
 */
static void delEmptyFiles() {
    File root = LittleFS.open("/");
    while (File f = root.openNextFile()) {
        if (!f.isDirectory() && f.size() == 0) {
            const char* path = f.path();
            f.close();
            LittleFS.remove(path);
        }
    }
}

/*
 - Löscht älteste Dateien bis 64KB frei sind.
 */
void cleanupStorage()
{
	delEmptyFiles();

	constexpr size_t MIN_FREE = 64 * 1024; // 64 KB reserve
	const size_t preLen = strlen(FILE_PREFIX);

	for (;;)
	{
		const size_t total = LittleFS.totalBytes();
		const size_t used = LittleFS.usedBytes();
		const size_t free = total - used;

		if (free >= MIN_FREE)
			return;

		File root = LittleFS.open("/");
		char oldest[64] = {0};
		time_t oldestTs = LLONG_MAX;

		for (File f = root.openNextFile(); f; f = root.openNextFile())
		{
			const char *name = f.path();
			if (endsWith(name, FILE_SUFFIX))
			{
				time_t ts = atoll(name + preLen);
				if (ts > 0 && ts < oldestTs)
				{
					oldestTs = ts;
					strlcpy(oldest, name, sizeof(oldest)); // vollständiger Pfad
				}
			}
		}
		root.close();

		if (!oldest[0])
			return;
		logi("Nur %d KB frei. Lösche %s", free / 1024, oldest);
		if (!LittleFS.remove(oldest))
			return;
	}
}

bool interval(uint32_t &last, const uint32_t interval_ms)
{
    const uint32_t now = millis();
    if (now - last >= interval_ms) {
        last += interval_ms;
        return true;
    }
    return false;
}
