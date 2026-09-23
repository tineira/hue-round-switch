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

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "0.5.26"
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

// 401 de consola, pegado en RAM. No va a NVS. Se limpia al boot.
inline volatile bool gConsoleAuthRejected = false;

inline void consoleNoteHttp(int code) {
  if (code == HTTP_CODE_UNAUTHORIZED) {
    gConsoleAuthRejected = true;
    return;
  }
  // Timeout, -1 o Wi-Fi caído (code <= 0) no despegan el 401.
  if (code > 0) {
    gConsoleAuthRejected = false;
  }
}

// Token y URL solo vienen de NVS console (HUESET desde la consola web).
inline const char *consoleToken() { return gConsoleTokNvs; }

inline const char *consoleUrl() { return gConsoleUrlNvs; }

// Snapshot/register/GET config en tarea propia. Ready no espera 4×20 s.
inline portMUX_TYPE gConsoleMux = portMUX_INITIALIZER_UNLOCKED;
inline bool gConsolePending = false;
inline bool gConsoleWorkerBusy = false;
inline bool gConsoleDoRegister = false;
inline TaskHandle_t gConsoleTask = nullptr;
inline String gConsoleConfigBody;
inline bool gConsoleConfigReady = false;
inline volatile uint32_t gConsoleEpoch = 1;
inline uint32_t gConsoleBodyEpoch = 0;

// Minted keys are hsw_ plus base64url. Anything else is not a console token.
inline bool consoleLooksLikeToken(const char *tok) {
  if (!tok || strncmp(tok, "hsw_", 4) != 0) {
    return false;
  }
  const size_t n = strlen(tok);
  if (n < 20 || n > 80) {
    return false;
  }
  for (size_t i = 4; i < n; i++) {
    const char c = tok[i];
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                    c == '-' || c == '_';
    if (!ok) {
      return false;
    }
  }
  return true;
}

inline bool consoleConfigured() {
  const char *url = consoleUrl();
  const char *tok = consoleToken();
  if (!url || !url[0] || !consoleLooksLikeToken(tok)) {
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
  // HUESET token nuevo despega el 401 antes de la próxima respuesta.
  gConsoleAuthRejected = false;
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
  char tokLocal[kConsoleTokMax];
  portENTER_CRITICAL(&gConsoleMux);
  strlcpy(tokLocal, gConsoleTokNvs, sizeof(tokLocal));
  portEXIT_CRITICAL(&gConsoleMux);
  const char *tok = tokLocal;
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

  http.addHeader("Authorization", String("Bearer ") + tok);
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
  consoleNoteHttp(code);
  return code;
}

inline bool consoleJobBusy() {
  portENTER_CRITICAL(&gConsoleMux);
  const bool busy = gConsolePending || gConsoleWorkerBusy;
  portEXIT_CRITICAL(&gConsoleMux);
  return busy;
}

inline void consoleJobPost(bool doRegister) {
  portENTER_CRITICAL(&gConsoleMux);
  if (doRegister) {
    gConsoleDoRegister = true;
  }
  gConsolePending = true;
  portEXIT_CRITICAL(&gConsoleMux);
  if (gConsoleTask) {
    xTaskNotifyGive(gConsoleTask);
  }
}

inline bool consoleRegister() {
  if (!consoleConfigured()) {
    return false;
  }
  hueStrLock();
  const String bid = gHueBridgeId;
  const String bip = gHueBridgeIp;
  hueStrUnlock();
  if (!bid.length() || !bip.length()) {
    LOGLN("console register skipped: no Bridge");
    return false;
  }

  String lights, rooms, scenes;
  if (!hueBuildSnapshot(&lights, &rooms, &scenes)) {
    LOGLN("console register skipped: Hue snapshot failed — last good snapshot kept");
    return false;
  }

  String payload;
  payload.reserve(lights.length() + rooms.length() + scenes.length() + 256);
  payload += "{\"mac\":";
  jsonAppendEscaped(payload, deviceMacHex().c_str());
  payload += ",\"firmware\":";
  jsonAppendEscaped(payload, FIRMWARE_VERSION);
  payload += ",\"bridgeid\":";
  jsonAppendEscaped(payload, bid.c_str());
  payload += ",\"bridge_ip\":";
  jsonAppendEscaped(payload, bip.c_str());
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

inline void consoleFetchConfigHttp() {
  if (!consoleConfigured()) {
    return;
  }
  const uint32_t epoch = gConsoleEpoch;
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
  if (epoch != gConsoleEpoch) {
    return;
  }
  gConsoleConfigBody = body;
  gConsoleBodyEpoch = epoch;
  if (epoch != gConsoleEpoch) {
    gConsoleConfigBody = "";
    gConsoleConfigReady = false;
    return;
  }
  gConsoleConfigReady = true;
}

inline void consoleForget() {
  portENTER_CRITICAL(&gConsoleMux);
  gConsoleTokNvs[0] = 0;
  gConsoleUrlNvs[0] = 0;
  gConsolePending = false;
  gConsoleDoRegister = false;
  portEXIT_CRITICAL(&gConsoleMux);
  gConsoleRegistered = false;
  gConsolePolledBoot = false;
  gConsoleAuthRejected = false;
  gNeedConsoleSync = false;
  gConsoleEpoch++;
  if (gConsoleEpoch == 0) {
    gConsoleEpoch = 1;
  }
  gConsoleConfigReady = false;
  Preferences prefs;
  if (prefs.begin("console", false)) {
    prefs.clear();
    prefs.end();
  }
}

inline void consoleApplyConfig(const char *body) {
  if (!body) {
    return;
  }
  const uint32_t localRev = gRecipeRev;
  const int remoteRev = jsonGetInt(body, "rev", -1);
  if (remoteRev < 0) {
    LOGLN("console config missing rev");
    return;
  }
  const bool force = gRecipesBidReset;
  if (!force && localRev >= static_cast<uint32_t>(remoteRev)) {
    LOG("console rev %u local %u — keep NVS\n", remoteRev, localRev);
    if (pagesParseTimeout(body)) {
      pagesSaveTimeout();
      LOG("console timeout %u (rev unchanged)\n", gScreenTimeoutSec);
    }
    return;
  }
  gRecipesBidReset = false;

  // No copiar recetas/páginas en el stack: HueRecipe×16 ~10 KB y loopTask son 8 KB.
  uint32_t rev = 0;
  if (!recipesParseConfig(body, &rev)) {
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

inline void consoleApplyConfigIfReady() {
  if (!gConsoleConfigReady) {
    return;
  }
  if (gConsoleBodyEpoch != gConsoleEpoch) {
    gConsoleConfigReady = false;
    gConsoleConfigBody = "";
    return;
  }
  String body = gConsoleConfigBody;
  gConsoleConfigBody = "";
  gConsoleConfigReady = false;
  consoleApplyConfig(body.c_str());
}

inline void consoleJobTask(void * /*arg*/) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    for (;;) {
      bool doRegister = false;
      portENTER_CRITICAL(&gConsoleMux);
      if (!gConsolePending) {
        gConsoleWorkerBusy = false;
        portEXIT_CRITICAL(&gConsoleMux);
        break;
      }
      gConsolePending = false;
      doRegister = gConsoleDoRegister;
      gConsoleDoRegister = false;
      gConsoleWorkerBusy = true;
      portEXIT_CRITICAL(&gConsoleMux);

      if (doRegister) {
        consoleRegister();
      }
      consoleFetchConfigHttp();
      while (gConsoleConfigReady) {
        vTaskDelay(pdMS_TO_TICKS(20));
      }
    }
  }
}

inline void consoleJobBegin() {
  if (gConsoleTask) {
    return;
  }
  xTaskCreatePinnedToCore(consoleJobTask, "consoleJob", 16384, nullptr, 1, &gConsoleTask, 0);
}

inline void consoleBootSync() {
  if (!consoleConfigured()) {
    LOGLN("console: token/url not set");
    return;
  }
  gConsoleLastPollMs = millis();
  gConsolePolledBoot = true;
  consoleJobPost(true);
}

inline void consolePollTick(unsigned long now) {
  consoleApplyConfigIfReady();
  if (!consoleConfigured() || WiFi.status() != WL_CONNECTED) {
    return;
  }
  if (consoleJobBusy() || gConsoleConfigReady) {
    return;
  }
  if (gTouchDown || gIdleWakeHold || gDimDragging || gTapWaitDouble) {
    return;
  }

  bool doRegister = false;
  bool due = false;
  if (gNeedConsoleSync) {
    gNeedConsoleSync = false;
    gConsoleRegistered = false;
    doRegister = true;
    due = true;
  } else {
    const unsigned long interval = (gRecipeCount == 0) ? kPollEmptyMs : kPollArmedMs;
    if (!gConsolePolledBoot || (now - gConsoleLastPollMs) >= interval) {
      due = true;
      doRegister = !gConsoleRegistered || gRecipeCount > 0;
    }
  }
  if (!due) {
    return;
  }
  gConsoleLastPollMs = now;
  gConsolePolledBoot = true;
  consoleJobPost(doRegister);
}
