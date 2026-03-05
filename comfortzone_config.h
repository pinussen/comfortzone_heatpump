#ifndef _COMFORTZONE_CONFIG_H
#define _COMFORTZONE_CONFIG_H

#define HP_PROTOCOL_1_6 160
#define HP_PROTOCOL_1_7 170
#define HP_PROTOCOL_1_8 180
#define HP_PROTOCOL_2_21 221

//#define HP_PROTOCOL HP_PROTOCOL_2_21

#ifndef HP_PROTOCOL
// Default protocol version is set to 1.6, but can be overridden
// here or by a compiler flag, e.g. -D HP_PROTOCOL=180
#define HP_PROTOCOL HP_PROTOCOL_1_6
#endif

// uncomment to enable debug mode
//#define DEBUG

#define COMFORTZONE_HEATPUMP_LAST_MESSAGE_BUFFER_SIZE 256

#if defined(USE_ESPHOME) && defined(ARDUINO)
#include <Arduino.h>
#define DPRINT(...) Serial.printf("[D][comfortzone] " __VA_ARGS__)
#define DPRINTLN(...) do { DPRINT(__VA_ARGS__); Serial.println(); } while (0)
#elif defined(USE_ESPHOME)
#include <esp_log.h>
#include <cstdio>
static const char *TAG = "comfortzone";

#ifndef HEX
#define HEX 16
#endif

inline void cz_log_print(const char *s) { ESP_LOGD(TAG, "%s", s); }
inline void cz_log_print(char *s) { ESP_LOGD(TAG, "%s", s); }
inline void cz_log_print(char c) { ESP_LOGD(TAG, "%c", c); }
inline void cz_log_print(int v) { ESP_LOGD(TAG, "%d", v); }
inline void cz_log_print(unsigned int v) { ESP_LOGD(TAG, "%u", v); }
inline void cz_log_print(long v) { ESP_LOGD(TAG, "%ld", v); }
inline void cz_log_print(unsigned long v) { ESP_LOGD(TAG, "%lu", v); }

template<typename T>
inline void cz_log_print(T v, int base) {
  if (base == HEX) {
    ESP_LOGD(TAG, "%X", static_cast<unsigned int>(v));
  } else {
    ESP_LOGD(TAG, "%d", static_cast<int>(v));
  }
}

template<typename... Args>
inline void cz_log_print(const char *fmt, Args... args) {
  char buf[256];
  std::snprintf(buf, sizeof(buf), fmt, args...);
  ESP_LOGD(TAG, "%s", buf);
}

#define DPRINT(args...)   cz_log_print(args)
#define DPRINTLN(args...) cz_log_print(args)
#elif defined(DEBUG)
#include "esphome/core/log.h"
static const char *TAG = "comfortzone";
#define DPRINT(args...)   ESP_LOGD(TAG, args)
#define DPRINTLN(args...) ESP_LOGD(TAG, args)
#else
#define DPRINT(args...)
#define DPRINTLN(args...)
#endif

// normal print
#if defined(OUTSER)
#define NPRINT(args...)    OUTSER.print(args)
#define NPRINTLN(args...)  OUTSER.println(args)
#else
#define NPRINT(args...)
#define NPRINTLN(args...)
#endif


#endif
