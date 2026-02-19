// Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#ifndef __MY_CUSTOM_LOG_H__
#define __MY_CUSTOM_LOG_H__
#include <stddef.h>

void initRamLogging();
void initTelnetLogging();

// Typ-Definition für einen Output-Stream (z.B. Telnet oder Serial)
typedef void (*LogOutputCallback)(const char *line);

// Registriert eine Funktion, die jede neue Log-Zeile empfängt
void setLogOutputInterface(LogOutputCallback cb);

// Gibt den kompletten aktuellen Puffer zurück
void getFullBuffer(void (*handler)(const char *data, size_t len));

#ifdef __cplusplus
extern "C"
{
#endif

#include "sdkconfig.h"
#include "esp_timer.h"
#include "rom/ets_sys.h"

// --- RAM LOG KONFIGURATION ---
#define RAM_LOG_SIZE 4096
	// extern char ram_log_buffer[RAM_LOG_SIZE];
	// extern int ram_logi(dx;

	// Eigene Print-Funktion
	int custom_ram_printf(const char *fmt, ...);
// Umleitung der internen Makros auf die neue Funktion
#define log_printf custom_ram_printf

#define ARDUHAL_LOG_LEVEL_NONE (0)
#define ARDUHAL_LOG_LEVEL_ERROR (1)
#define ARDUHAL_LOG_LEVEL_WARN (2)
#define ARDUHAL_LOG_LEVEL_INFO (3)
#define ARDUHAL_LOG_LEVEL_DEBUG (4)
#define ARDUHAL_LOG_LEVEL_VERBOSE (5)

#ifndef CONFIG_ARDUHAL_LOG_DEFAULT_LEVEL
#define CONFIG_ARDUHAL_LOG_DEFAULT_LEVEL ARDUHAL_LOG_LEVEL_NONE
#endif

#ifndef CORE_DEBUG_LEVEL
#define ARDUHAL_LOG_LEVEL CONFIG_ARDUHAL_LOG_DEFAULT_LEVEL
#else
#define ARDUHAL_LOG_LEVEL CORE_DEBUG_LEVEL
#ifdef USE_ESP_IDF_LOG
#ifndef LOG_LOCAL_LEVEL
#define LOG_LOCAL_LEVEL CORE_DEBUG_LEVEL
#endif
#endif
#endif

#ifndef CONFIG_ARDUHAL_LOG_COLORS
#define CONFIG_ARDUHAL_LOG_COLORS 0
#endif

#if CONFIG_ARDUHAL_LOG_COLORS
#define ARDUHAL_LOG_COLOR_BLACK "30"
#define ARDUHAL_LOG_COLOR_RED "31"	  // ERROR
#define ARDUHAL_LOG_COLOR_GREEN "32"  // INFO
#define ARDUHAL_LOG_COLOR_YELLOW "33" // WARNING
#define ARDUHAL_LOG_COLOR_BLUE "34"
#define ARDUHAL_LOG_COLOR_MAGENTA "35"
#define ARDUHAL_LOG_COLOR_CYAN "36" // DEBUG
#define ARDUHAL_LOG_COLOR_GRAY "37" // VERBOSE
#define ARDUHAL_LOG_COLOR_WHITE "38"

#define ARDUHAL_LOG_COLOR(COLOR) "\033[0;" COLOR "m"
#define ARDUHAL_LOG_BOLD(COLOR) "\033[1;" COLOR "m"
#define ARDUHAL_LOG_RESET_COLOR "\033[0m"

#define ARDUHAL_LOG_COLOR_E ARDUHAL_LOG_COLOR(ARDUHAL_LOG_COLOR_RED)
#define ARDUHAL_LOG_COLOR_W ARDUHAL_LOG_COLOR(ARDUHAL_LOG_COLOR_YELLOW)
#define ARDUHAL_LOG_COLOR_I ARDUHAL_LOG_COLOR(ARDUHAL_LOG_COLOR_GREEN)
#define ARDUHAL_LOG_COLOR_D ARDUHAL_LOG_COLOR(ARDUHAL_LOG_COLOR_CYAN)
#define ARDUHAL_LOG_COLOR_V ARDUHAL_LOG_COLOR(ARDUHAL_LOG_COLOR_GRAY)
#define ARDUHAL_LOG_COLOR_PRINT(letter) log_printf(ARDUHAL_LOG_COLOR_##letter)
#define ARDUHAL_LOG_COLOR_PRINT_END log_printf(ARDUHAL_LOG_RESET_COLOR)
#else
#define ARDUHAL_LOG_COLOR_E
#define ARDUHAL_LOG_COLOR_W
#define ARDUHAL_LOG_COLOR_I
#define ARDUHAL_LOG_COLOR_D
#define ARDUHAL_LOG_COLOR_V
#define ARDUHAL_LOG_RESET_COLOR
#define ARDUHAL_LOG_COLOR_PRINT(letter)
#define ARDUHAL_LOG_COLOR_PRINT_END
#endif

#ifdef USE_ESP_IDF_LOG
#ifndef ARDUHAL_ESP_LOG_TAG
#define ARDUHAL_ESP_LOG_TAG "ARDUINO"
#endif
#endif

	const char *pathToFileName(const char *path);
	int log_printf(const char *fmt, ...);
	void log_print_buf(const uint8_t *b, size_t len);

#define ARDUHAL_SHORT_LOG_FORMAT(letter, format) ARDUHAL_LOG_COLOR_##letter format ARDUHAL_LOG_RESET_COLOR "\r\n"
#define ARDUHAL_LOG_FORMAT(letter, format)                                                                                                                \
	ARDUHAL_LOG_COLOR_##letter "[%6u][" #letter "][%s:%u] %s(): " format ARDUHAL_LOG_RESET_COLOR "\r\n", (unsigned long)(esp_timer_get_time() / 1000ULL), \
		pathToFileName(__FILE__), __LINE__, __FUNCTION__

// --- VERBOSE ---
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_VERBOSE
#ifndef USE_ESP_IDF_LOG
#define logv(format, ...) log_printf(ARDUHAL_LOG_FORMAT(V, format), ##__VA_ARGS__)
#define isr_logv(format, ...) ets_printf(ARDUHAL_LOG_FORMAT(V, format), ##__VA_ARGS__)
#define log_bufv(b, l)               \
	do                               \
	{                                \
		ARDUHAL_LOG_COLOR_PRINT(V);  \
		log_print_buf(b, l);         \
		ARDUHAL_LOG_COLOR_PRINT_END; \
	} while (0)
#else
#define logv(format, ...)                                                                 \
	do                                                                                    \
	{                                                                                     \
		ESP_LOG_LEVEL_LOCAL(ESP_LOG_VERBOSE, ARDUHAL_ESP_LOG_TAG, format, ##__VA_ARGS__); \
	} while (0)
#define isr_logv(format, ...)                                                                       \
	do                                                                                              \
	{                                                                                               \
		ets_printf(LOG_FORMAT(V, format), esp_log_timestamp(), ARDUHAL_ESP_LOG_TAG, ##__VA_ARGS__); \
	} while (0)
#define log_bufv(b, l)                                                      \
	do                                                                      \
	{                                                                       \
		ESP_LOG_BUFFER_HEXDUMP(ARDUHAL_ESP_LOG_TAG, b, l, ESP_LOG_VERBOSE); \
	} while (0)
#endif
#else
#define logv(format, ...) \
	do                    \
	{                     \
	} while (0)
#define isr_logv(format, ...) \
	do                        \
	{                         \
	} while (0)
#define log_bufv(b, l) \
	do                 \
	{                  \
	} while (0)
#endif

// --- DEBUG ---
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_DEBUG
#ifndef USE_ESP_IDF_LOG
#define logd(format, ...) log_printf(ARDUHAL_LOG_FORMAT(D, format), ##__VA_ARGS__)
#define isr_logd(format, ...) ets_printf(ARDUHAL_LOG_FORMAT(D, format), ##__VA_ARGS__)
#define log_bufd(b, l)               \
	do                               \
	{                                \
		ARDUHAL_LOG_COLOR_PRINT(D);  \
		log_print_buf(b, l);         \
		ARDUHAL_LOG_COLOR_PRINT_END; \
	} while (0)
#else
#define logd(format, ...)                                                               \
	do                                                                                  \
	{                                                                                   \
		ESP_LOG_LEVEL_LOCAL(ESP_LOG_DEBUG, ARDUHAL_ESP_LOG_TAG, format, ##__VA_ARGS__); \
	} while (0)
#define isr_logd(format, ...)                                                                       \
	do                                                                                              \
	{                                                                                               \
		ets_printf(LOG_FORMAT(D, format), esp_log_timestamp(), ARDUHAL_ESP_LOG_TAG, ##__VA_ARGS__); \
	} while (0)
#define log_bufd(b, l)                                                    \
	do                                                                    \
	{                                                                     \
		ESP_LOG_BUFFER_HEXDUMP(ARDUHAL_ESP_LOG_TAG, b, l, ESP_LOG_DEBUG); \
	} while (0)
#endif
#else
#define logd(format, ...) \
	do                    \
	{                     \
	} while (0)
#define isr_logd(format, ...) \
	do                        \
	{                         \
	} while (0)
#define log_bufd(b, l) \
	do                 \
	{                  \
	} while (0)
#endif

// --- INFO ---
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
#ifndef USE_ESP_IDF_LOG
#define logi(format, ...) log_printf(ARDUHAL_LOG_FORMAT(I, format), ##__VA_ARGS__)
#define isr_logi(format, ...) ets_printf(ARDUHAL_LOG_FORMAT(I, format), ##__VA_ARGS__)
#define log_bufi(b, l)               \
	do                               \
	{                                \
		ARDUHAL_LOG_COLOR_PRINT(I);  \
		log_print_buf(b, l);         \
		ARDUHAL_LOG_COLOR_PRINT_END; \
	} while (0)
#else
#define logi(format, ...)                                                              \
	do                                                                                 \
	{                                                                                  \
		ESP_LOG_LEVEL_LOCAL(ESP_LOG_INFO, ARDUHAL_ESP_LOG_TAG, format, ##__VA_ARGS__); \
	} while (0)
#define isr_logi(format, ...)                                                                       \
	do                                                                                              \
	{                                                                                               \
		ets_printf(LOG_FORMAT(I, format), esp_log_timestamp(), ARDUHAL_ESP_LOG_TAG, ##__VA_ARGS__); \
	} while (0)
#define log_bufi(b, l)                                                   \
	do                                                                   \
	{                                                                    \
		ESP_LOG_BUFFER_HEXDUMP(ARDUHAL_ESP_LOG_TAG, b, l, ESP_LOG_INFO); \
	} while (0)
#endif
#else
#define logi(format, ...) \
	do                    \
	{                     \
	} while (0)
#define isr_logi(format, ...) \
	do                        \
	{                         \
	} while (0)
#define log_bufi(b, l) \
	do                 \
	{                  \
	} while (0)
#endif

// --- WARN ---
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_WARN
#ifndef USE_ESP_IDF_LOG
#define logw(format, ...) log_printf(ARDUHAL_LOG_FORMAT(W, format), ##__VA_ARGS__)
#define isr_logw(format, ...) ets_printf(ARDUHAL_LOG_FORMAT(W, format), ##__VA_ARGS__)
#define log_bufw(b, l)               \
	do                               \
	{                                \
		ARDUHAL_LOG_COLOR_PRINT(W);  \
		log_print_buf(b, l);         \
		ARDUHAL_LOG_COLOR_PRINT_END; \
	} while (0)
#else
#define logw(format, ...)                                                              \
	do                                                                                 \
	{                                                                                  \
		ESP_LOG_LEVEL_LOCAL(ESP_LOG_WARN, ARDUHAL_ESP_LOG_TAG, format, ##__VA_ARGS__); \
	} while (0)
#define isr_logw(format, ...)                                                                       \
	do                                                                                              \
	{                                                                                               \
		ets_printf(LOG_FORMAT(W, format), esp_log_timestamp(), ARDUHAL_ESP_LOG_TAG, ##__VA_ARGS__); \
	} while (0)
#define log_bufw(b, l)                                                   \
	do                                                                   \
	{                                                                    \
		ESP_LOG_BUFFER_HEXDUMP(ARDUHAL_ESP_LOG_TAG, b, l, ESP_LOG_WARN); \
	} while (0)
#endif
#else
#define logw(format, ...) \
	do                    \
	{                     \
	} while (0)
#define isr_logw(format, ...) \
	do                        \
	{                         \
	} while (0)
#define log_bufw(b, l) \
	do                 \
	{                  \
	} while (0)
#endif

// --- ERROR ---
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_ERROR
#ifndef USE_ESP_IDF_LOG
#define loge(format, ...) log_printf(ARDUHAL_LOG_FORMAT(E, format), ##__VA_ARGS__)
#define isr_loge(format, ...) ets_printf(ARDUHAL_LOG_FORMAT(E, format), ##__VA_ARGS__)
#define log_bufe(b, l)               \
	do                               \
	{                                \
		ARDUHAL_LOG_COLOR_PRINT(E);  \
		log_print_buf(b, l);         \
		ARDUHAL_LOG_COLOR_PRINT_END; \
	} while (0)
#else
#define loge(format, ...)                                                               \
	do                                                                                  \
	{                                                                                   \
		ESP_LOG_LEVEL_LOCAL(ESP_LOG_ERROR, ARDUHAL_ESP_LOG_TAG, format, ##__VA_ARGS__); \
	} while (0)
#define isr_loge(format, ...)                                                                       \
	do                                                                                              \
	{                                                                                               \
		ets_printf(LOG_FORMAT(E, format), esp_log_timestamp(), ARDUHAL_ESP_LOG_TAG, ##__VA_ARGS__); \
	} while (0)
#define log_bufe(b, l)                                                    \
	do                                                                    \
	{                                                                     \
		ESP_LOG_BUFFER_HEXDUMP(ARDUHAL_ESP_LOG_TAG, b, l, ESP_LOG_ERROR); \
	} while (0)
#endif
#else
#define loge(format, ...) \
	do                    \
	{                     \
	} while (0)
#define isr_loge(format, ...) \
	do                        \
	{                         \
	} while (0)
#define log_bufe(b, l) \
	do                 \
	{                  \
	} while (0)
#endif

// --- NONE / DEFAULT ---
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_NONE
#ifndef USE_ESP_IDF_LOG
#define logn(format, ...) log_printf(ARDUHAL_LOG_FORMAT(E, format), ##__VA_ARGS__)
#define isr_logn(format, ...) ets_printf(ARDUHAL_LOG_FORMAT(E, format), ##__VA_ARGS__)
#define log_bufn(b, l)               \
	do                               \
	{                                \
		ARDUHAL_LOG_COLOR_PRINT(E);  \
		log_print_buf(b, l);         \
		ARDUHAL_LOG_COLOR_PRINT_END; \
	} while (0)
#else
#define logn(format, ...)                                                               \
	do                                                                                  \
	{                                                                                   \
		ESP_LOG_LEVEL_LOCAL(ESP_LOG_ERROR, ARDUHAL_ESP_LOG_TAG, format, ##__VA_ARGS__); \
	} while (0)
#define isr_logn(format, ...)                                                                       \
	do                                                                                              \
	{                                                                                               \
		ets_printf(LOG_FORMAT(E, format), esp_log_timestamp(), ARDUHAL_ESP_LOG_TAG, ##__VA_ARGS__); \
	} while (0)
#define log_bufn(b, l)                                                    \
	do                                                                    \
	{                                                                     \
		ESP_LOG_BUFFER_HEXDUMP(ARDUHAL_ESP_LOG_TAG, b, l, ESP_LOG_ERROR); \
	} while (0)
#endif
#else
#define logn(format, ...) \
	do                    \
	{                     \
	} while (0)
#define isr_logn(format, ...) \
	do                        \
	{                         \
	} while (0)
#define log_bufn(b, l) \
	do                 \
	{                  \
	} while (0)
#endif

#include "esp_log.h"

#ifndef USE_ESP_IDF_LOG
#ifdef CONFIG_ARDUHAL_ESP_LOG
#undef ESP_LOGE
#undef ESP_LOGW
#undef ESP_LOGI
#undef ESP_LOGD
#undef ESP_LOGV
#undef ESP_EARLY_LOGE
#undef ESP_EARLY_LOGW
#undef ESP_EARLY_LOGI
#undef ESP_EARLY_LOGD
#undef ESP_EARLY_LOGV

#define ESP_LOGE(tag, format, ...) loge("[%s] " format, tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, format, ...) logw("[%s] " format, tag, ##__VA_ARGS__)
#define ESP_LOGI(tag, format, ...) logi("[%s] " format, tag, ##__VA_ARGS__)
#define ESP_LOGD(tag, format, ...) logd("[%s] " format, tag, ##__VA_ARGS__)
#define ESP_LOGV(tag, format, ...) logv("[%s] " format, tag, ##__VA_ARGS__)
#define ESP_EARLY_LOGE(tag, format, ...) isr_loge("[%s] " format, tag, ##__VA_ARGS__)
#define ESP_EARLY_LOGW(tag, format, ...) isr_logw("[%s] " format, tag, ##__VA_ARGS__)
#define ESP_EARLY_LOGI(tag, format, ...) isr_logi("[%s] " format, tag, ##__VA_ARGS__)
#define ESP_EARLY_LOGD(tag, format, ...) isr_logd("[%s] " format, tag, ##__VA_ARGS__)
#define ESP_EARLY_LOGV(tag, format, ...) isr_logv("[%s] " format, tag, ##__VA_ARGS__)
#endif
#endif

#ifdef __cplusplus
}
#endif

#endif /* __MY_CUSTOM_LOG_H__ */
