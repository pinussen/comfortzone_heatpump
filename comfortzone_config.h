#ifndef _COMFORTZONE_CONFIG_H
#define _COMFORTZONE_CONFIG_H

// Force cache invalidation - commit 2

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

#undef DEBUG
// uncomment to enable debug mode
//#define DEBUG

#define COMFORTZONE_HEATPUMP_LAST_MESSAGE_BUFFER_SIZE 256

#ifdef USE_ESPHOME
// When used in ESPHome, disable internal debug macros
// ESPHome components should handle logging at a higher level
#define DPRINT(...)
#define DPRINTLN(...)
#endif

#if defined(DEBUG) && !defined(USE_ESPHOME)
#include "esphome/core/log.h"
static const char *TAG = "comfortzone";
#define DPRINT(args...)    ESP_LOGD(TAG, args)
#define DPRINTLN(args...)  ESP_LOGD(TAG, args)
#elif !defined(DPRINT)
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
