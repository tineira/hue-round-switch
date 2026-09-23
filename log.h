#pragma once

// SERIAL_DEBUG in config.h (0 = no USB log). Serial.begin always runs (Improv / HUESET).
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
