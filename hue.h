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
  if (withKey && gHueAppKey.length()) {
    http.addHeader("hue-application-key", gHueAppKey);
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
  if (response) {
    *response = http.getString();
  }
  http.end();
  return code;
}

inline int hueClipStream(const char *resource, JsonDataSink &sink) {
  if (!gHueBridgeIp.length() || !gHueAppKey.length() || !resource) {
    return -1;
  }
  String url = "https://";
  url += gHueBridgeIp;
  url += "/clip/v2/resource/";
  url += resource;
  NetworkClientSecure client;
  client.setInsecure();
  HTTPClient http;
  if (!http.begin(client, url)) {
    return -1;
  }
  http.setTimeout(20000);
  http.addHeader("hue-application-key", gHueAppKey);
  const int code = http.GET();
  if (code == HTTP_CODE_OK) {
    http.writeToStream(&sink);
  }
  http.end();
  return code;
}

inline String hueResourceUrl(const char *rtype, const char *rid) {
  String url = "https://";
  url += gHueBridgeIp;
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
  if (!gHueBridgeIp.length() || !gHueAppKey.length() || !rtype || !rid || !on) {
    Serial.println("Hue GET: begin failed");
    return false;
  }
  String body;
  const int code = hueHttp(hueResourceUrl(rtype, rid), "GET", nullptr, &body, true, true);
  Serial.printf("Hue GET %s/%s %d\n", rtype, rid, code);
  if (code != HTTP_CODE_OK) {
    Serial.println(body);
    return false;
  }
  if (!jsonHueOn(body.c_str(), on)) {
    Serial.println("Hue GET: could not parse on");
    Serial.println(body);
    return false;
  }
  return true;
}

inline bool hueSetOn(const char *rtype, const char *rid, bool on) {
  if (!gHueBridgeIp.length() || !gHueAppKey.length() || !rtype || !rid) {
    Serial.println("Hue PUT: begin failed");
    return false;
  }
  const char *payload = on ? "{\"on\":{\"on\":true}}" : "{\"on\":{\"on\":false}}";
  String body;
  const int code = hueHttp(hueResourceUrl(rtype, rid), "PUT", payload, &body, true, true);
  Serial.printf("Hue PUT %s/%s %d -> %s\n", rtype, rid, code, on ? "on" : "off");
  if (code != HTTP_CODE_OK) {
    Serial.println(body);
    return false;
  }
  return true;
}

inline bool hueSceneActive(const char *rid, bool *active) {
  if (!gHueBridgeIp.length() || !gHueAppKey.length() || !rid || !rid[0] || !active) {
    return false;
  }
  String body;
  const int code = hueHttp(hueResourceUrl("scene", rid), "GET", nullptr, &body, true, true, 2500);
  if (code != HTTP_CODE_OK) {
    Serial.printf("Hue GET scene/%s %d\n", rid, code);
    return false;
  }
  return jsonHueSceneActive(body.c_str(), active);
}

inline bool hueRecallScene(const char *rid) {
  if (!gHueBridgeIp.length() || !gHueAppKey.length() || !rid) {
    Serial.println("Hue recall: begin failed");
    return false;
  }
  String body;
  const int code =
      hueHttp(hueResourceUrl("scene", rid), "PUT", "{\"recall\":{\"action\":\"active\"}}", &body, true, true);
  Serial.printf("Hue recall scene/%s %d\n", rid, code);
  if (code != HTTP_CODE_OK) {
    Serial.println(body);
    return false;
  }
  return true;
}

inline bool hueGetLightState(const char *rtype, const char *rid, bool *on, int *pct) {
  if (!gHueBridgeIp.length() || !gHueAppKey.length() || !rtype || !rid) {
    return false;
  }
  String body;
  const int code = hueHttp(hueResourceUrl(rtype, rid), "GET", nullptr, &body, true, true, 4000);
  if (code != HTTP_CODE_OK) {
    Serial.printf("Hue GET state %s/%s %d\n", rtype, rid, code);
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

inline bool hueSetBrightness(const char *rtype, const char *rid, int pct) {
  if (!gHueBridgeIp.length() || !gHueAppKey.length() || !rtype || !rid) {
    return false;
  }
  if (pct < 1) {
    pct = 1;
  }
  if (pct > 100) {
    pct = 100;
  }
  char payload[72];
  snprintf(payload, sizeof(payload), "{\"on\":{\"on\":true},\"dimming\":{\"brightness\":%d}}", pct);
  String body;
  const int code = hueHttp(hueResourceUrl(rtype, rid), "PUT", payload, &body, true, true, 4000);
  if (code != HTTP_CODE_OK) {
    Serial.printf("Hue PUT dim %s/%s %d -> %d %s\n", rtype, rid, code, pct, body.c_str());
    return false;
  }
  Serial.printf("Hue dim %d%%\n", pct);
  return true;
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
  Serial.printf("Hue execute: unknown action %s\n", action);
  return false;
}
