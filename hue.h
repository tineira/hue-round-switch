#pragma once

#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include "config.h"
#include "json_util.h"

#ifndef HUE_BRIDGE_IP
#define HUE_BRIDGE_IP ""
#endif
#ifndef HUE_APP_KEY
#define HUE_APP_KEY ""
#endif

// IP y application key en runtime (mDNS / NVS / emparejado).
extern String gHueBridgeIp;
extern String gHueAppKey;

// Copia corta bajo mutex: el re-pair USB y HUECLR escriben estas String
// desde otro contexto que hueHttp.
inline SemaphoreHandle_t gHueStrMux = nullptr;

inline void hueStrEnsure() {
  if (!gHueStrMux) {
    gHueStrMux = xSemaphoreCreateMutex();
  }
}

inline void hueStrLock() {
  if (gHueStrMux) {
    xSemaphoreTake(gHueStrMux, portMAX_DELAY);
  }
}

inline void hueStrUnlock() {
  if (gHueStrMux) {
    xSemaphoreGive(gHueStrMux);
  }
}

inline void hueSetAppKey(const String &v) {
  hueStrLock();
  gHueAppKey = v;
  hueStrUnlock();
}

inline void hueSetBridgeIp(const String &v) {
  hueStrLock();
  gHueBridgeIp = v;
  hueStrUnlock();
}

inline bool hueRamReady() {
  hueStrLock();
  const bool ok = gHueBridgeIp.length() > 0 && gHueAppKey.length() > 0;
  hueStrUnlock();
  return ok;
}

// 401/403 con la application key: la key ya no sirve. RAM, se pierde al boot.
// Discovery o /api/config (sin key) no cuentan. Un 200 con key recupera.
// Timeout, 5xx o Bridge caído no ponen ni quitan la bandera.
inline volatile bool gHueAuthRejected = false;
inline unsigned long gHueAuthGraceUntil = 0;

inline void hueAuthGraceArm(unsigned long ms) {
  gHueAuthGraceUntil = millis() + ms;
  gHueAuthRejected = false;
}

inline bool hueAuthGraceOpen() {
  return (long)(gHueAuthGraceUntil - millis()) > 0;
}

inline void hueNoteAuth(int code, const String *body, bool withKey) {
  if (code == HTTP_CODE_UNAUTHORIZED || code == HTTP_CODE_FORBIDDEN) {
    if (!withKey || hueAuthGraceOpen()) {
      return;
    }
    if (body && body->indexOf("link button not pressed") >= 0) {
      return;
    }
    gHueAuthRejected = true;
    return;
  }
  if (withKey && code == HTTP_CODE_OK) {
    gHueAuthRejected = false;
  }
}

// El Bridge usa un certificado propio; Clip v2 exige HTTPS local.
// setInsecure() evita validar esa CA (solo LAN, no cloud).

inline int hueHttp(const String &url, const char *method, const char *body, String *response, bool withKey,
                   bool insecure, int timeoutMs = 8000) {
  NetworkClientSecure client;
  if (insecure) {
    client.setInsecure();
  } else {
    client.useBuiltinCACertBundle();
  }
  HTTPClient http;
  if (!http.begin(client, url)) {
    return -1;
  }
  http.setTimeout(timeoutMs > 0 ? timeoutMs : 8000);
  String keyCopy;
  if (withKey) {
    hueStrLock();
    keyCopy = gHueAppKey;
    hueStrUnlock();
  }
  const bool sentKey = withKey && keyCopy.length() > 0;
  if (sentKey) {
    http.addHeader("hue-application-key", keyCopy);
  }
  if (body) {
    http.addHeader("Content-Type", "application/json");
  }
  int code = -1;
  if (strcmp(method, "GET") == 0) {
    code = http.GET();
  } else if (strcmp(method, "POST") == 0) {
    code = http.POST(body ? String(body) : String());
  } else {
    code = http.PUT(body ? String(body) : String());
  }
  String denied;
  const String *noted = nullptr;
  if (response) {
    *response = http.getString();
    noted = response;
  } else if (code == HTTP_CODE_UNAUTHORIZED || code == HTTP_CODE_FORBIDDEN) {
    denied = http.getString();
    noted = &denied;
  }
  http.end();
  hueNoteAuth(code, noted, sentKey);
  return code;
}

inline int hueClipStream(const char *resource, JsonDataSink &sink) {
  hueStrLock();
  const String ip = gHueBridgeIp;
  const String key = gHueAppKey;
  hueStrUnlock();
  if (!ip.length() || !key.length() || !resource) {
    return -1;
  }
  String url = "https://";
  url += ip;
  url += "/clip/v2/resource/";
  url += resource;
  NetworkClientSecure client;
  client.setInsecure();
  HTTPClient http;
  if (!http.begin(client, url)) {
    return -1;
  }
  http.setTimeout(20000);
  http.addHeader("hue-application-key", key);
  const int code = http.GET();
  String denied;
  const String *noted = nullptr;
  if (code == HTTP_CODE_OK) {
    http.writeToStream(&sink);
  } else if (code == HTTP_CODE_UNAUTHORIZED || code == HTTP_CODE_FORBIDDEN) {
    denied = http.getString();
    noted = &denied;
  }
  http.end();
  hueNoteAuth(code, noted, true);
  return code;
}

inline String hueResourceUrl(const char *rtype, const char *rid) {
  hueStrLock();
  const String ip = gHueBridgeIp;
  hueStrUnlock();
  String url = "https://";
  url += ip;
  url += "/clip/v2/resource/";
  url += rtype;
  url += "/";
  url += rid;
  return url;
}

inline bool hueParseOn(const String &body, bool *on) {
  return jsonHueOn(body.c_str(), on);
}

inline bool hueGetOn(const char *rtype, const char *rid, bool *on) {
  if (!hueRamReady() || !rtype || !rid || !on) {
    LOGLN("Hue GET: begin failed");
    return false;
  }
  String body;
  const int code = hueHttp(hueResourceUrl(rtype, rid), "GET", nullptr, &body, true, true);
  LOG("Hue GET %s/%s %d\n", rtype, rid, code);
  if (code != HTTP_CODE_OK) {
    LOGLN(body);
    return false;
  }
  if (!jsonHueOn(body.c_str(), on)) {
    LOGLN("Hue GET: could not parse on");
    LOGLN(body);
    return false;
  }
  return true;
}

inline bool hueSetOn(const char *rtype, const char *rid, bool on) {
  if (!hueRamReady() || !rtype || !rid) {
    LOGLN("Hue PUT: begin failed");
    return false;
  }
  const char *payload = on ? "{\"on\":{\"on\":true}}" : "{\"on\":{\"on\":false}}";
  String body;
  const int code = hueHttp(hueResourceUrl(rtype, rid), "PUT", payload, &body, true, true);
  LOG("Hue PUT %s/%s %d -> %s\n", rtype, rid, code, on ? "on" : "off");
  if (code != HTTP_CODE_OK) {
    LOGLN(body);
    return false;
  }
  return true;
}

inline bool hueSceneActive(const char *rid, bool *active) {
  if (!hueRamReady() || !rid || !rid[0] || !active) {
    return false;
  }
  String body;
  const int code = hueHttp(hueResourceUrl("scene", rid), "GET", nullptr, &body, true, true, 2500);
  if (code != HTTP_CODE_OK) {
    LOG("Hue GET scene/%s %d\n", rid, code);
    return false;
  }
  return jsonHueSceneActive(body.c_str(), active);
}

inline int hueRecallSceneHttp(const char *rid) {
  if (!hueRamReady() || !rid || !rid[0]) {
    LOGLN("Hue recall: begin failed");
    return -1;
  }
  String body;
  const int code =
      hueHttp(hueResourceUrl("scene", rid), "PUT", "{\"recall\":{\"action\":\"active\"}}", &body, true, true);
  LOG("Hue recall scene/%s %d\n", rid, code);
  if (code != HTTP_CODE_OK && code != HTTP_CODE_NOT_FOUND) {
    LOGLN(body);
  }
  return code;
}

inline bool hueRecallScene(const char *rid) { return hueRecallSceneHttp(rid) == HTTP_CODE_OK; }

inline bool hueGetLightState(const char *rtype, const char *rid, bool *on, int *pct) {
  if (!hueRamReady() || !rtype || !rid || !rid[0]) {
    return false;
  }
  String body;
  const int code = hueHttp(hueResourceUrl(rtype, rid), "GET", nullptr, &body, true, true, 4000);
  if (code == HTTP_CODE_NOT_FOUND) {
    LOG("Hue GET state %s/%s 404 skip\n", rtype, rid);
    return false;
  }
  if (code != HTTP_CODE_OK) {
    LOG("Hue GET state %s/%s %d\n", rtype, rid, code);
    return false;
  }
  bool parsed = false;
  if (on && jsonHueOn(body.c_str(), on)) {
    parsed = true;
  }
  if (pct && jsonHueBrightness(body.c_str(), pct)) {
    if (*pct < 1) {
      *pct = 1;
    }
    parsed = true;
  }
  return parsed;
}

inline bool hueGetBrightness(const char *rtype, const char *rid, int *pct) {
  return hueGetLightState(rtype, rid, nullptr, pct);
}

// PUT de brillo. turnOn agrega on.on=true (set lights todo off). 404 se salta.
inline bool huePutDimming(const char *rtype, const char *rid, int pct, bool turnOn) {
  if (!hueRamReady() || !rtype || !rid || !rid[0]) {
    return false;
  }
  if (pct < 1) {
    pct = 1;
  }
  if (pct > 100) {
    pct = 100;
  }
  char payload[80];
  if (turnOn) {
    snprintf(payload, sizeof(payload), "{\"on\":{\"on\":true},\"dimming\":{\"brightness\":%d}}", pct);
  } else {
    snprintf(payload, sizeof(payload), "{\"dimming\":{\"brightness\":%d}}", pct);
  }
  String body;
  const int code = hueHttp(hueResourceUrl(rtype, rid), "PUT", payload, &body, true, true, 4000);
  if (code == HTTP_CODE_NOT_FOUND) {
    LOG("Hue PUT dim %s/%s 404 skip\n", rtype, rid);
    return false;
  }
  if (code != HTTP_CODE_OK) {
    LOG("Hue PUT dim %s/%s %d -> %d %s\n", rtype, rid, code, pct, body.c_str());
    return false;
  }
  LOG("Hue dim %s/%s %d%%\n", rtype, rid, pct);
  return true;
}

inline bool hueSetBrightness(const char *rtype, const char *rid, int pct) {
  return huePutDimming(rtype, rid, pct, true);
}

inline bool hueToggle(const char *rtype, const char *rid, bool *nowOn) {
  bool on = false;
  if (!hueGetOn(rtype, rid, &on)) {
    return false;
  }
  if (!hueSetOn(rtype, rid, !on)) {
    return false;
  }
  if (nowOn) {
    *nowOn = !on;
  }
  return true;
}

inline bool hueExecute(const char *action, const char *rtype, const char *rid, bool *nowOn = nullptr) {
  if (!action || !rtype || !rid || !rid[0]) {
    return false;
  }
  if (strcmp(action, "on") == 0) {
    if (!hueSetOn(rtype, rid, true)) {
      return false;
    }
    if (nowOn) {
      *nowOn = true;
    }
    return true;
  }
  if (strcmp(action, "off") == 0) {
    if (!hueSetOn(rtype, rid, false)) {
      return false;
    }
    if (nowOn) {
      *nowOn = false;
    }
    return true;
  }
  if (strcmp(action, "recall_scene") == 0) {
    if (!hueRecallScene(rid)) {
      return false;
    }
    if (nowOn) {
      *nowOn = true;
    }
    return true;
  }
  if (strcmp(action, "toggle") == 0) {
    return hueToggle(rtype, rid, nowOn);
  }
  LOG("Hue execute: unknown action %s\n", action);
  return false;
}
