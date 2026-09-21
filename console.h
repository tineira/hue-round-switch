#pragma once

#include <WiFi.h>
#include <HTTPClient.h>
#include <NetworkClient.h>
#include <NetworkClientSecure.h>
#include <Preferences.h>
#include <string.h>
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
static const size_t kConsoleTokMax = 128;
static const size_t kConsoleUrlMax = 128;

inline bool gConsoleRegistered = false;
inline unsigned long gConsoleLastPollMs = 0;
inline bool gConsolePolledBoot = false;
inline char gConsoleTokNvs[kConsoleTokMax] = {0};
inline char gConsoleUrlNvs[kConsoleUrlMax] = {0};

inline const char *consoleToken() {
  if (gConsoleTokNvs[0]) {
    return gConsoleTokNvs;
  }
  return CONSOLE_TOKEN;
}

inline const char *consoleUrl() {
  if (gConsoleUrlNvs[0]) {
    return gConsoleUrlNvs;
  }
  return CONSOLE_URL;
}

inline bool consoleConfigured() {
  const char *url = consoleUrl();
  const char *tok = consoleToken();
  if (!url || !url[0] || !tok || !tok[0]) {
    return false;
  }
  if (strstr(tok, "your-")) {
    return false;
  }
  return true;
}

inline void consoleLoadNvs() {
  gConsoleTokNvs[0] = 0;
  gConsoleUrlNvs[0] = 0;
  Preferences prefs;
  if (!prefs.begin("console", true)) {
    return;
  }
  const String tok = prefs.getString("token", "");
  const String url = prefs.getString("url", "");
  prefs.end();
  if (tok.length() && tok.length() < kConsoleTokMax) {
    memcpy(gConsoleTokNvs, tok.c_str(), tok.length() + 1);
  }
  if (url.length() && url.length() < kConsoleUrlMax) {
    memcpy(gConsoleUrlNvs, url.c_str(), url.length() + 1);
  }
}

inline bool consoleSetToken(const char *tok) {
  if (!tok || !tok[0] || strlen(tok) >= kConsoleTokMax) {
    return false;
  }
  Preferences prefs;
  if (!prefs.begin("console", false)) {
    return false;
  }
  prefs.putString("token", tok);
  prefs.end();
  memcpy(gConsoleTokNvs, tok, strlen(tok) + 1);
  return true;
}

inline bool consoleSetUrl(const char *url) {
  if (!url || !url[0] || strlen(url) >= kConsoleUrlMax) {
    return false;
  }
  if (strncmp(url, "https://", 8) != 0 && strncmp(url, "http://", 7) != 0) {
    return false;
  }
  Preferences prefs;
  if (!prefs.begin("console", false)) {
    return false;
  }
  prefs.putString("url", url);
  prefs.end();
  memcpy(gConsoleUrlNvs, url, strlen(url) + 1);
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
  String url = consoleUrl();
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

  http.addHeader("Authorization", String("Bearer ") + consoleToken());
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
    if (pagesParseTimeout(body.c_str())) {
      pagesSaveTimeout();
      LOG("console timeout %u (rev unchanged)\n", gScreenTimeoutSec);
    }
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
    LOGLN("console: token/url not set");
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
