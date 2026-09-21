#pragma once

// SERIAL_DEBUG en config.h (0 = sin USB log). Serial.begin va siempre (Improv / HUESET).
#ifndef SERIAL_DEBUG
#define SERIAL_DEBUG 0
#endif

#if SERIAL_DEBUG
#define LOG(...) Serial.printf(__VA_ARGS__)
#define LOGS(s) Serial.print(s)
#define LOGLN(s) Serial.println(s)
#else
#define LOG(...) ((void)0)
#define LOGS(s) ((void)0)
#define LOGLN(s) ((void)0)
#endif
