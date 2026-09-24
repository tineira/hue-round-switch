#pragma once

#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <atomic>
#include "config.h"
#include "json_util.h"

#ifndef HUE_BRIDGE_IP
#define HUE_BRIDGE_IP ""
#endif
#ifndef HUE_APP_KEY
#define HUE_APP_KEY ""
#endif

// IP and application key at runtime (mDNS / NVS / pairing).
extern String gHueBridgeIp;
extern String gHueAppKey;

// Short copy under a mutex: USB HUECLR / HUEPAIR write these Strings from the loop,
// the hueJob task reads them.
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

// Link to the Bridge. Only the hueJob task writes it; the loop reads it to pick a screen.
enum HueLink : uint8_t {
  LINK_START = 0,    // Not reached yet since boot (or since HUECLR): "Loading"
  LINK_SEARCHING,    // Bridge not found or not answering: "No Bridge", retried with backoff
  LINK_PAIRING,      // No key, or the key was rejected twice: "Press Bridge button"
  LINK_READY,        // A keyed request returned 200
  LINK_UNREACHABLE,  // Was READY; the last keyed request timed out or got a 5xx. Ready stays.
};

inline std::atomic<uint8_t> gHueLink{LINK_START};
// Consecutive 401/403 with the key (outside the grace window). Two flip READY to PAIRING.
inline uint8_t gHueRejectCount = 0;
// The saved key was rejected (not "no key"). PAIRING then also re-tries the old key.
inline std::atomic<bool> gHueKeyRejected{false};
inline unsigned long gHueAuthGraceUntil = 0;

inline void hueAuthGraceArm(unsigned long ms) {
  gHueAuthGraceUntil = millis() + ms;
  gHueRejectCount = 0;
  gHueKeyRejected = false;
}

inline bool hueAuthGraceOpen() { return (long)(gHueAuthGraceUntil - millis()) > 0; }

inline bool hueLinkUsable() {
  const uint8_t l = gHueLink;
  return l == LINK_READY || l == LINK_UNREACHABLE;
}

// Every keyed request reports here (hueJob task only).
// 2xx → READY. Timeout / -1 / 5xx → UNREACHABLE (only from READY). Two 401/403 → PAIRING.
inline void hueNoteAuth(int code, const String *body, bool withKey) {
  if (!withKey) {
    return;
  }
  if (code == HTTP_CODE_UNAUTHORIZED || code == HTTP_CODE_FORBIDDEN) {
    if (hueAuthGraceOpen()) {
      return;
    }
    if (body && body->indexOf("link button not pressed") >= 0) {
      return;
    }
    if (gHueRejectCount < 255) {
      gHueRejectCount++;
    }
    LOG("Hue key rejected (%d) x%u\n", code, gHueRejectCount);
    if (gHueRejectCount >= 2 && hueLinkUsable()) {
      gHueKeyRejected = true;
      gHueLink = LINK_PAIRING;
    }
    return;
  }
  if (code >= 200 && code < 300) {
    gHueRejectCount = 0;
    if (gHueLink == LINK_UNREACHABLE) {
      LOGLN("Hue link back");
      gHueLink = LINK_READY;
    }
    return;
  }
  if ((code < 0 || code >= 500) && gHueLink == LINK_READY) {
    LOG("Hue link unreachable (%d)\n", code);
    gHueLink = LINK_UNREACHABLE;
  }
}

// One HTTPS connection to the Bridge, kept alive between requests. Only the hueJob task
// touches it. The Bridge uses a self-signed certificate; Clip v2 requires local HTTPS,
// so setInsecure() skips validating that CA (LAN only, not cloud).
static const unsigned long kHueConnIdleMs = 60000;

inline NetworkClientSecure *gHueTls = nullptr;
inline HTTPClient *gHueConnHttp = nullptr;
inline String gHueConnHost;
inline unsigned long gHueConnLastMs = 0;

inline void hueConnClose() {
  if (gHueTls) {
    gHueTls->stop();
  }
  gHueConnHost = "";
}

inline void hueConnIdleCheck() {
  if (gHueConnHost.length() && millis() - gHueConnLastMs >= kHueConnIdleMs) {
    hueConnClose();
  }
}

inline bool hueUrlHost(const String &url, String *host) {
  if (!url.startsWith("https://")) {
    return false;
  }
  int end = url.indexOf('/', 8);
  if (end < 0) {
    end = url.length();
  }
  *host = url.substring(8, end);
  return host->length() > 0;
}

// Keyed request on the kept-alive connection. A reused socket the Bridge already closed
// fails fast with a negative code; that case is retried once on a fresh connection.
inline int hueConnRequest(const String &url, const char *method, const char *body, String *response,
                          Stream *sink, int timeoutMs) {
  if (!gHueTls) {
    gHueTls = new NetworkClientSecure();
    gHueTls->setInsecure();
    gHueConnHttp = new HTTPClient();
  }
  String host;
  if (!hueUrlHost(url, &host)) {
    return -1;
  }
  if (gHueConnHost.length() && gHueConnHost != host) {
    hueConnClose();
  }
  hueConnIdleCheck();
  hueStrLock();
  const String key = gHueAppKey;
  hueStrUnlock();
  const bool sentKey = key.length() > 0;

  int code = -1;
  String denied;
  for (uint8_t attempt = 0; attempt < 2; attempt++) {
    const bool reused = gHueTls->connected();
    const unsigned long t0 = millis();
    HTTPClient &http = *gHueConnHttp;
    if (!http.begin(*gHueTls, url)) {
      return -1;
    }
    http.setReuse(true);
    http.setConnectTimeout(3000);
    http.setTimeout(timeoutMs > 0 ? timeoutMs : 8000);
    if (sentKey) {
      http.addHeader("hue-application-key", key);
    }
    if (body) {
      http.addHeader("Content-Type", "application/json");
    }
    if (strcmp(method, "GET") == 0) {
      code = http.GET();
    } else if (strcmp(method, "POST") == 0) {
      code = http.POST(body ? String(body) : String());
    } else {
      code = http.PUT(body ? String(body) : String());
    }
    if (code > 0) {
      if (sink && code == HTTP_CODE_OK) {
        http.writeToStream(sink);
      } else if (response) {
        *response = http.getString();
      } else {
        denied = http.getString();
      }
    }
    http.end();
    LOG("hue %s %s %d %lums %s\n", method, url.substring(8 + host.length()).c_str(), code, millis() - t0,
        reused ? "reuse" : "new");
    if (code > 0) {
      gHueConnHost = host;
      gHueConnLastMs = millis();
      break;
    }
    hueConnClose();
    if (!reused) {
      break;
    }
  }
  hueNoteAuth(code, response ? response : &denied, sentKey);
  return code;
}

// One-shot request with its own TLS session: /api/config probe, pairing POST,
// discovery.meethue.com. The kept-alive Bridge connection is closed first so there is
// never more than one session to the Bridge.
inline int hueHttpOnce(const String &url, const char *method, const char *body, String *response, bool insecure,
                       int timeoutMs = 8000) {
  hueConnClose();
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
  http.setReuse(false);
  http.setConnectTimeout(3000);
  http.setTimeout(timeoutMs > 0 ? timeoutMs : 8000);
  if (body) {
    http.addHeader("Content-Type", "application/json");
  }
  int code = -1;
  if (strcmp(method, "GET") == 0) {
    code = http.GET();
  } else {
    code = http.POST(body ? String(body) : String());
  }
  if (response) {
    *response = code > 0 ? http.getString() : String();
  }
  http.end();
  return code;
}

// Keyed Clip v2 request to the Bridge (hueJob task only).
inline int hueHttp(const String &url, const char *method, const char *body, String *response,
                   int timeoutMs = 8000) {
  return hueConnRequest(url, method, body, response, nullptr, timeoutMs);
}

inline int hueClipStream(const char *resource, Stream &sink) {
  hueStrLock();
  const String ip = gHueBridgeIp;
  const bool haveKey = gHueAppKey.length() > 0;
  hueStrUnlock();
  if (!ip.length() || !haveKey || !resource) {
    return -1;
  }
  String url = "https://";
  url += ip;
  url += "/clip/v2/resource/";
  url += resource;
  return hueConnRequest(url, "GET", nullptr, nullptr, &sink, 20000);
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

inline bool hueGetOn(const char *rtype, const char *rid, bool *on) {
  if (!hueRamReady() || !rtype || !rid || !on) {
    return false;
  }
  String body;
  const int code = hueHttp(hueResourceUrl(rtype, rid), "GET", nullptr, &body, 4000);
  if (code != HTTP_CODE_OK) {
    LOGLN(body);
    return false;
  }
  if (!jsonHueOn(body.c_str(), on)) {
    LOGLN("Hue GET: could not parse on");
    return false;
  }
  return true;
}

inline bool hueSetOn(const char *rtype, const char *rid, bool on) {
  if (!hueRamReady() || !rtype || !rid) {
    return false;
  }
  const char *payload = on ? "{\"on\":{\"on\":true}}" : "{\"on\":{\"on\":false}}";
  String body;
  const int code = hueHttp(hueResourceUrl(rtype, rid), "PUT", payload, &body);
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
  const int code = hueHttp(hueResourceUrl("scene", rid), "GET", nullptr, &body, 2500);
  if (code != HTTP_CODE_OK) {
    return false;
  }
  return jsonHueSceneActive(body.c_str(), active);
}

inline int hueRecallSceneHttp(const char *rid) {
  if (!hueRamReady() || !rid || !rid[0]) {
    return -1;
  }
  String body;
  const int code = hueHttp(hueResourceUrl("scene", rid), "PUT", "{\"recall\":{\"action\":\"active\"}}", &body);
  if (code != HTTP_CODE_OK && code != HTTP_CODE_NOT_FOUND) {
    LOGLN(body);
  }
  return code;
}

inline bool hueGetLightState(const char *rtype, const char *rid, bool *on, int *pct) {
  if (!hueRamReady() || !rtype || !rid || !rid[0]) {
    return false;
  }
  String body;
  const int code = hueHttp(hueResourceUrl(rtype, rid), "GET", nullptr, &body, 4000);
  if (code != HTTP_CODE_OK) {
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

// Brightness PUT. turnOn adds on.on=true (light set all off). 404 is skipped.
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
  const int code = hueHttp(hueResourceUrl(rtype, rid), "PUT", payload, &body, 4000);
  if (code != HTTP_CODE_OK) {
    LOG("Hue PUT dim %s/%s %d -> %d %s\n", rtype, rid, code, pct, body.c_str());
    return false;
  }
  return true;
}
