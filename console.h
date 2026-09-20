#pragma once

#include <WiFi.h>
#include <HTTPClient.h>
#include <NetworkClient.h>
#include <NetworkClientSecure.h>
#include "config.h"
#include "channels.h"
#include "recipes.h"
#include "snapshot.h"

#ifndef CONSOLE_URL
#define CONSOLE_URL ""
#endif
#ifndef CONSOLE_TOKEN
#define CONSOLE_TOKEN ""
#endif
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "0.1.0"
#endif

static const unsigned long kPollEmptyMs = 60UL * 1000UL;
static const unsigned long kPollArmedMs = 60UL * 60UL * 1000UL;

inline bool gConsoleRegistered = false;
inline unsigned long gConsoleLastPollMs = 0;
inline bool gConsolePolledBoot = false;

inline bool consoleConfigured() {
  const char *url = CONSOLE_URL;
  const char *tok = CONSOLE_TOKEN;
  if (!url || !url[0] || !tok || !tok[0]) {
    return false;
  }
  if (strstr(tok, "your-")) {
    return false;
  }
  return true;
}

inline String deviceMacHex() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char buf[13];
  snprintf(buf, sizeof(buf), "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}

inline String consoleBaseUrl() {
  String url = CONSOLE_URL;
  while (url.endsWith("/")) {
    url.remove(url.length() - 1);
  }
  return url;
}

inline int consoleHttp(const char *method, const String &path, const char *body, String *response) {
  const String url = consoleBaseUrl() + path;
  HTTPClient http;
  http.setTimeout(15000);

  NetworkClientSecure secure;
  NetworkClient plain;
  bool began = false;
  if (url.startsWith("https://")) {
    secure.useBuiltinCACertBundle();
    began = http.begin(secure, url);
  } else {
    began = http.begin(plain, url);
  }
  if (!began) {
    return -1;
  }

  http.addHeader("Authorization", String("Bearer ") + CONSOLE_TOKEN);
  http.addHeader("Content-Type", "application/json");

  int code = -1;
  if (strcmp(method, "GET") == 0) {
    code = http.GET();
  } else {
    code = http.POST(body ? String(body) : String("{}"));
  }
  if (response) {
    *response = http.getString();
  }
  http.end();
  return code;
}

inline bool consoleRegister() {
  if (!consoleConfigured()) {
    return false;
  }
  if (!gHueBridgeId.length() || !gHueBridgeIp.length()) {
    LOGLN("console register skipped: no Bridge");
    return false;
  }

  String lights, rooms, scenes;
  hueBuildSnapshot(&lights, &rooms, &scenes);

  String payload;
  payload.reserve(lights.length() + rooms.length() + scenes.length() + 256);
  payload += "{\"mac\":";
  jsonAppendEscaped(payload, deviceMacHex().c_str());
  payload += ",\"firmware\":";
  jsonAppendEscaped(payload, FIRMWARE_VERSION);
  payload += ",\"bridgeid\":";
  jsonAppendEscaped(payload, gHueBridgeId.c_str());
  payload += ",\"bridge_ip\":";
  jsonAppendEscaped(payload, gHueBridgeIp.c_str());
  payload += ",\"product\":\"round\",\"source\":\"xiao\",\"channels\":";
  channelsAppendJson(payload);
  payload += ",\"lights\":";
  payload += lights;
  payload += ",\"rooms\":";
  payload += rooms;
  payload += ",\"scenes\":";
  payload += scenes;
  payload += '}';

  String body;
  const int code = consoleHttp("POST", "/api/device/register", payload.c_str(), &body);
  LOG("console POST register %d\n", code);
  if (code != HTTP_CODE_OK) {
    LOGLN(body);
    return false;
  }
  gConsoleRegistered = true;
  LOG("console registered mac=%s\n", deviceMacHex().c_str());
  return true;
}

inline void consoleFetchConfig() {
  if (!consoleConfigured()) {
    return;
  }
  String path = "/api/device/config?mac=";
  path += deviceMacHex();
  String body;
  const int code = consoleHttp("GET", path, nullptr, &body);
  LOG("console GET config %d\n", code);
  if (code != HTTP_CODE_OK) {
    if (code == HTTP_CODE_UNAUTHORIZED) {
      LOGLN("console unauthorized — NVS recipes kept");
    }
    if (body.length()) {
      LOGLN(body);
    }
    return;
  }

  const uint32_t localRev = gRecipeRev;
  const int remoteRev = jsonGetInt(body.c_str(), "rev", -1);
  if (remoteRev < 0) {
    LOGLN("console config missing rev");
    return;
  }
  if (localRev >= static_cast<uint32_t>(remoteRev)) {
    LOG("console rev %u local %u — keep NVS\n", remoteRev, localRev);
    pagesFillDimFromRecipes();
    return;
  }

  // No copiar recetas/páginas en el stack: HueRecipe×16 ~10 KB y loopTask son 8 KB.
  uint32_t rev = 0;
  if (!recipesParseConfig(body.c_str(), &rev)) {
    recipesLoad();
    pagesLoad();
    LOGLN("console config parse failed — NVS restored");
    return;
  }
  gRecipeRev = rev;
  recipesSave();
  pagesSave();
  gNeedHueState = true;
  gNeedFullPaint = true;
  LOG("console rev %u — replaced %u pages %u recipes\n", gRecipeRev, gPageCount, gRecipeCount);
}

inline void consoleBootSync() {
  if (!consoleConfigured()) {
    LOGLN("console: CONSOLE_URL / CONSOLE_TOKEN not set");
    return;
  }
  consoleRegister();
  consoleFetchConfig();
  gConsoleLastPollMs = millis();
  gConsolePolledBoot = true;
}

inline void consolePollTick(unsigned long now) {
  if (!consoleConfigured() || WiFi.status() != WL_CONNECTED) {
    return;
  }
  if (gNeedConsoleSync) {
    gNeedConsoleSync = false;
    gConsoleRegistered = false;
    consoleRegister();
    consoleFetchConfig();
    gConsoleLastPollMs = now;
    gConsolePolledBoot = true;
    return;
  }

  const unsigned long interval = (gRecipeCount == 0) ? kPollEmptyMs : kPollArmedMs;
  if (gConsolePolledBoot && (now - gConsoleLastPollMs) < interval) {
    return;
  }
  gConsoleLastPollMs = now;
  gConsolePolledBoot = true;

  if (!gConsoleRegistered || gRecipeCount > 0) {
    consoleRegister();
  }
  consoleFetchConfig();
}
