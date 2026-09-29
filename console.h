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
#include "hue_job.h"
#include "snapshot.h"

// The version lives only in hue-round-switch.ino, defined before this header is included.
#ifndef FIRMWARE_VERSION
#error "FIRMWARE_VERSION must be defined in hue-round-switch.ino"
#endif

static const unsigned long kPollEmptyMs = 60UL * 1000UL;
static const unsigned long kPollArmedMs = 60UL * 60UL * 1000UL;
// X-Poll-Sec from the console is clamped to this range (config-sync spec §2.3).
static const uint32_t kPollSecMin = 30;
static const uint32_t kPollSecMax = 3600;
// Topology (register) keeps the old hourly cadence even when the console polls faster.
static const unsigned long kRegisterEveryMs = 60UL * 60UL * 1000UL;
static const size_t kConsoleTokMax = 128;
static const size_t kConsoleUrlMax = 128;

inline bool gConsoleRegistered = false;
inline unsigned long gConsoleLastPollMs = 0;
inline unsigned long gConsoleLastRegisterMs = 0;
inline bool gConsolePolledBoot = false;
// Written by the console task after each config poll: seconds until the next poll, 0 = the
// console did not say (older console), use kPollEmptyMs / kPollArmedMs.
inline std::atomic<uint32_t> gConsolePollSec{0};
// Loop-owned: set when consoleApplyConfig replaced NVS, so the next tick polls once more at
// once and reports the new rev (§2.4).
inline bool gConsoleConfirmPoll = false;
inline char gConsoleTokNvs[kConsoleTokMax] = {0};
inline char gConsoleUrlNvs[kConsoleUrlMax] = {0};

inline void consoleNoteHttp(int code) {
  if (code == HTTP_CODE_UNAUTHORIZED) {
    gConsoleAuthRejected = true;
    return;
  }
  // Timeout, -1 or Wi-Fi down (code <= 0) do not clear the 401.
  if (code > 0) {
    gConsoleAuthRejected = false;
  }
}

// Token and URL only come from NVS console (HUESET from the web console).
inline const char *consoleToken() { return gConsoleTokNvs; }

inline const char *consoleUrl() { return gConsoleUrlNvs; }

// Register / GET config in their own task (the snapshot itself is built by the hueJob task).
// The config body reaches the loop through a queue; the loop owns pages and recipes.
inline portMUX_TYPE gConsoleMux = portMUX_INITIALIZER_UNLOCKED;
inline bool gConsolePending = false;
inline bool gConsoleWorkerBusy = false;
inline bool gConsoleDoRegister = false;
inline TaskHandle_t gConsoleTask = nullptr;

struct ConsoleConfigMsg {
  String *body;
  uint32_t epoch;
};

inline QueueHandle_t gConsoleConfigQ = nullptr;
inline std::atomic<uint32_t> gConsoleEpoch{1};

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
  // A new HUESET token clears the 401 before the next response.
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

// X-Poll-Sec as whole seconds clamped to kPollSecMin..kPollSecMax, 0 if missing or not a number.
inline uint32_t consoleParsePollSec(const String &h) {
  const char *s = h.c_str();
  while (*s == ' ') {
    s++;
  }
  if (*s < '0' || *s > '9') {
    return 0;
  }
  char *end = nullptr;
  const unsigned long v = strtoul(s, &end, 10);
  while (end && *end == ' ') {
    end++;
  }
  if (!end || *end) {
    return 0;
  }
  if (v < kPollSecMin) {
    return kPollSecMin;
  }
  return v > kPollSecMax ? kPollSecMax : static_cast<uint32_t>(v);
}

// pollSec (optional): X-Poll-Sec of a 200 or 204, parsed by consoleParsePollSec.
inline int consoleHttp(const char *method, const String &path, const char *body, String *response,
                       uint32_t *pollSec = nullptr) {
  char tokLocal[kConsoleTokMax];
  portENTER_CRITICAL(&gConsoleMux);
  strlcpy(tokLocal, gConsoleTokNvs, sizeof(tokLocal));
  portEXIT_CRITICAL(&gConsoleMux);
  const char *tok = tokLocal;
  if (pollSec) {
    *pollSec = 0;
  }
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
  if (pollSec) {
    static const char *kPollHeader[] = {"X-Poll-Sec"};
    http.collectHeaders(kPollHeader, 1);
  }

  int code = -1;
  if (strcmp(method, "GET") == 0) {
    code = http.GET();
  } else {
    code = http.POST(body ? String(body) : String("{}"));
  }
  if (pollSec && (code == HTTP_CODE_OK || code == HTTP_CODE_NO_CONTENT)) {
    *pollSec = consoleParsePollSec(http.header("X-Poll-Sec"));
  }
  // 204 has no body; reading one without Content-Length would wait for the socket to close.
  if (response && code != HTTP_CODE_NO_CONTENT) {
    *response = http.getString();
  }
  http.end();
  consoleNoteHttp(code);
  return code;
}

#include "ota.h"

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
  if (!hueJobSnapshot(&lights, &rooms, &scenes, 180000UL)) {
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
  // rev lets the console answer 204 when nothing changed. Read after any apply the loop did
  // before posting this job (the post goes through gConsoleMux). After a Bridge change the
  // loop wants the full body even at an equal rev, so rev is left out then.
  if (!gRecipesBidReset) {
    char revBuf[16];
    snprintf(revBuf, sizeof(revBuf), "&rev=%lu", static_cast<unsigned long>(gRecipeRev));
    path += revBuf;
  }
  otaAppendQuery(path);
  String body;
  uint32_t pollSec = 0;
  const int code = consoleHttp("GET", path, nullptr, &body, &pollSec);
  LOG("console GET config %d poll %us\n", code, static_cast<unsigned>(pollSec));
  if (code == HTTP_CODE_OK || code == HTTP_CODE_NO_CONTENT) {
    gConsolePollSec = pollSec;
    otaPollAccepted();
  } else if (code == HTTP_CODE_UNAUTHORIZED) {
    gConsolePollSec = kPollSecMax;
  }
  // Other errors keep the last interval, as before.
  if (code == HTTP_CODE_NO_CONTENT) {
    // Same rev as NVS: keep it, nothing to parse or hand to the loop.
    return;
  }
  if (code != HTTP_CODE_OK) {
    if (code == HTTP_CODE_UNAUTHORIZED) {
      LOGLN("console unauthorized — NVS recipes kept");
    }
    if (body.length()) {
      LOGLN(body);
    }
    return;
  }
  // Whatever the rev: while an update is offered the console answers 200 with an unchanged rev.
  otaParseOffer(body.c_str());
  if (epoch != gConsoleEpoch || !gConsoleConfigQ) {
    return;
  }
  ConsoleConfigMsg msg{new String(body), epoch};
  // Waits until the loop took the previous body: never two polls in flight.
  xQueueSend(gConsoleConfigQ, &msg, portMAX_DELAY);
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
  gConsoleConfirmPoll = false;
  gConsolePollSec = 0;
  gConsoleEpoch++;
  if (gConsoleEpoch == 0) {
    gConsoleEpoch = 1;
  }
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

  // Do not copy recipes/pages onto the stack: HueRecipe×16 is ~10 KB and loopTask has 8 KB.
  uint32_t rev = 0;
  if (!recipesParseConfig(body, &rev)) {
    recipesLoad();
    pagesLoad();
    LOGLN("console config parse failed — NVS restored");
    return;
  }
  gRecipeRev = rev;
  // Recipes and pages first, the rev only after both (recipesSaveRev).
  const bool saved = recipesSaveData() && pagesSave() && recipesSaveRev();
  gNeedHueState = true;
  gNeedFullPaint = true;
  // Poll once more right away so the console sees the new rev (answered with 204). After a
  // failed write NVS keeps the old rev, so the next poll brings this config again.
  gConsoleConfirmPoll = saved;
  if (!saved) {
    LOGLN("console config NVS write failed — rev not stored");
  }
  LOG("console rev %u — replaced %u pages %u recipes\n", gRecipeRev, gPageCount, gRecipeCount);
}

inline void consoleApplyConfigIfReady() {
  ConsoleConfigMsg msg;
  if (!gConsoleConfigQ || xQueueReceive(gConsoleConfigQ, &msg, 0) != pdTRUE) {
    return;
  }
  if (msg.body && msg.epoch == gConsoleEpoch) {
    consoleApplyConfig(msg.body->c_str());
  }
  delete msg.body;
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
      gOtaOfferValid = false;
      consoleFetchConfigHttp();
      // The poll's connection is closed and its body is the loop's: the download runs alone.
      if (consoleConfigured()) {
        otaMaybeApply();
      }
      gOtaOfferValid = false;
    }
  }
}

inline void consoleJobBegin() {
  if (gConsoleTask) {
    return;
  }
  gConsoleConfigQ = xQueueCreate(1, sizeof(ConsoleConfigMsg));
  xTaskCreatePinnedToCore(consoleJobTask, "consoleJob", 16384, nullptr, 1, &gConsoleTask, 0);
}

inline void consoleBootSync() {
  if (!consoleConfigured()) {
    LOGLN("console: token/url not set");
    return;
  }
  gConsoleLastPollMs = millis();
  gConsoleLastRegisterMs = gConsoleLastPollMs;
  gConsolePolledBoot = true;
  consoleJobPost(true);
}

inline void consolePollTick(unsigned long now) {
  consoleApplyConfigIfReady();
  if (!consoleConfigured() || WiFi.status() != WL_CONNECTED) {
    return;
  }
  if (consoleJobBusy()) {
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
  } else if (gConsoleConfirmPoll) {
    // Config only. The job reads gRecipeRev, already the rev just applied.
    due = true;
  } else {
    // The console paces polls (X-Poll-Sec); an older console leaves the old constants.
    const uint32_t pollSec = gConsolePollSec;
    const unsigned long interval =
        pollSec ? pollSec * 1000UL : ((gRecipeCount == 0) ? kPollEmptyMs : kPollArmedMs);
    if (!gConsolePolledBoot || (now - gConsoleLastPollMs) >= interval) {
      due = true;
      // Topology stays hourly when armed, however often config is polled.
      doRegister = !gConsoleRegistered ||
                   (gRecipeCount > 0 && (now - gConsoleLastRegisterMs) >= kRegisterEveryMs);
    }
  }
  if (!due) {
    return;
  }
  gConsoleConfirmPoll = false;
  gConsoleLastPollMs = now;
  gConsolePolledBoot = true;
  if (doRegister) {
    gConsoleLastRegisterMs = now;
  }
  consoleJobPost(doRegister);
}
