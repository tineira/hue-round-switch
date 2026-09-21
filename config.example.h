#pragma once

// Copy this file to config.h and fill in real values.
// config.h is gitignored and must not be committed.
//
// Product USB installer binaries compile with empty WIFI_* / CONSOLE_* strings.
// Arduino STA remembers Wi-Fi from Improv. NVS namespace console holds token/url
// from HUESET. These #defines are the dev fallback when those are empty.

#define WIFI_SSID "your-ssid"
#define WIFI_PASSWORD "your-password"

// Device console (not the Hue Bridge). Token is minted in the console UI.
// CONSOLE_URL    public host, e.g. https://hue.tineira.com
//                (local dev: http://localhost:3000 — no TLS)
// CONSOLE_TOKEN  device API key (hsw_…), sent as Authorization: Bearer
// Do not put SSID, passwords, Hue keys, or this token in git.
#define CONSOLE_URL "https://hue.tineira.com"
#define CONSOLE_TOKEN "your-console-token"

// 1 = USB serial logs (Serial Monitor). 0 = no logs (product). CDC is always on.
#ifndef SERIAL_DEBUG
#define SERIAL_DEBUG 0
#endif

// Bridge IP, Hue application key, and recipes are not stored here
// (mDNS / pair / NVS / poll).
