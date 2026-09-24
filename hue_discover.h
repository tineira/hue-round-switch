#pragma once

#include <ESPmDNS.h>
#include <Preferences.h>
#include <atomic>
#include "hue.h"
#include "json_util.h"

// Discover the Bridge (cached IP, mDNS _hue._tcp, cloud) and pair the application key.
// IP and key are saved in NVS so a DHCP change does not require a rebuild.
// Everything here runs in the hueJob task, except the NVS helpers the USB parser calls.

static const unsigned long kLongPressMs = 3000;

inline String gHueBridgeId;

// Loop (USB / BOOT) → hueJob task.
inline std::atomic<bool> gHuePairReq{false};
inline std::atomic<bool> gHuePairCancel{false};
inline std::atomic<uint32_t> gHueClrEpoch{1};
inline std::atomic<uint32_t> gHuePairEpoch{0};
inline std::atomic<bool> gWifiStaForgotten{false};

inline bool huePairBusy() { return gHuePairReq || gHueLink == LINK_PAIRING; }

inline void hueSetBridgeId(const String &v) {
  hueStrLock();
  gHueBridgeId = v;
  hueStrUnlock();
}

inline void hueZeroRam() {
  hueStrLock();
  gHueBridgeIp = "";
  gHueAppKey = "";
  gHueBridgeId = "";
  hueStrUnlock();
}

inline bool hueLooksLikeIp(const String &s) {
  if (s.length() < 7 || s.indexOf('x') >= 0) {
    return false;
  }
  int dots = 0;
  for (unsigned i = 0; i < s.length(); i++) {
    const char c = s[i];
    if (c == '.') {
      dots++;
    } else if (c < '0' || c > '9') {
      return false;
    }
  }
  return dots == 3;
}

inline bool hueLooksLikeKey(const String &s) { return s.length() >= 20 && s.indexOf("your-") < 0; }

inline bool hueProbeBridge(const String &ip, String *bridgeId) {
  String body;
  const int code = hueHttpOnce("https://" + ip + "/api/config", "GET", nullptr, &body, true, 4000);
  if (code != HTTP_CODE_OK) {
    return false;
  }
  String id;
  if (!jsonStringField(body, "bridgeid", &id)) {
    return false;
  }
  if (bridgeId) {
    *bridgeId = id;
  }
  return true;
}

inline void hueLoadStore() {
  Preferences prefs;
  prefs.begin("hue", true);
  String ip = prefs.getString("ip", "");
  String key = prefs.getString("key", "");
  const String id = prefs.getString("bid", "");
  prefs.end();

  if (!hueLooksLikeIp(ip) && hueLooksLikeIp(HUE_BRIDGE_IP)) {
    ip = HUE_BRIDGE_IP;
  }
  if (!hueLooksLikeKey(key) && hueLooksLikeKey(HUE_APP_KEY)) {
    key = HUE_APP_KEY;
  }
  hueStrLock();
  gHueBridgeIp = ip;
  gHueAppKey = hueLooksLikeKey(key) ? key : String();
  gHueBridgeId = id;
  hueStrUnlock();
}

// Only the saved application key. IP and bridge id stay for discovery.
inline void hueClearSavedKey() {
  hueSetAppKey("");
  Preferences prefs;
  if (!prefs.begin("hue", false)) {
    return;
  }
  prefs.putString("key", "");
  prefs.end();
}

inline void hueForgetSaved() {
  Preferences prefs;
  if (prefs.begin("hue", false)) {
    prefs.clear();
    prefs.end();
  }
  hueZeroRam();
}

inline void hueSaveStore() {
  if (gHuePairCancel) {
    return;
  }
  const uint32_t epoch = gHueClrEpoch;
  hueStrLock();
  const String ip = gHueBridgeIp;
  const String key = gHueAppKey;
  const String id = gHueBridgeId;
  hueStrUnlock();
  if (gHuePairCancel || epoch != gHueClrEpoch) {
    return;
  }
  Preferences prefs;
  if (!prefs.begin("hue", false)) {
    return;
  }
  // Called on every READY from setup: skip the flash write when nothing changed.
  if (prefs.getString("ip", "") == ip && prefs.getString("key", "") == key && prefs.getString("bid", "") == id) {
    prefs.end();
    return;
  }
  prefs.putString("ip", ip);
  prefs.putString("key", key);
  prefs.putString("bid", id);
  prefs.end();
  if (gHuePairCancel || epoch != gHueClrEpoch) {
    Preferences wipe;
    if (wipe.begin("hue", false)) {
      wipe.clear();
      wipe.end();
    }
  }
}

inline bool gMdnsStarted = false;

inline bool hueDiscoverMdns() {
  if (gHuePairCancel) {
    return false;
  }
  hueStrLock();
  const String wantId = gHueBridgeId;
  hueStrUnlock();
  // mdns_init() fails when called twice: start it once and keep it.
  if (!gMdnsStarted) {
    String host = "hue-sw-";
    host += String((uint16_t)(ESP.getEfuseMac() & 0xFFFF), HEX);
    if (!MDNS.begin(host.c_str())) {
      LOGLN("mDNS begin failed");
      return false;
    }
    gMdnsStarted = true;
  }

  const int n = MDNS.queryService("hue", "tcp");
  LOG("mDNS _hue._tcp: %d\n", n);
  String chosen;
  for (int i = 0; i < n; i++) {
    if (gHuePairCancel) {
      return false;
    }
    const IPAddress ip = MDNS.address(i);
    if (ip == IPAddress()) {
      continue;
    }
    const String ipStr = ip.toString();
    const String bid = MDNS.hasTxt(i, "bridgeid") ? MDNS.txt(i, "bridgeid") : String();
    LOG("  %s  %s  bridgeid=%s\n", MDNS.instanceName(i).c_str(), ipStr.c_str(), bid.c_str());
    if (wantId.length() && bid.length() && bid.equalsIgnoreCase(wantId)) {
      chosen = ipStr;
      break;
    }
    if (!chosen.length()) {
      chosen = ipStr;
    }
  }

  if (!chosen.length()) {
    return false;
  }
  hueSetBridgeIp(chosen);
  return true;
}

inline bool hueDiscoverCloud() {
  String body;
  const int code = hueHttpOnce("https://discovery.meethue.com/", "GET", nullptr, &body, false);
  LOG("discovery.meethue.com %d\n", code);
  if (code != HTTP_CODE_OK) {
    return false;
  }
  String ip;
  if (!jsonStringField(body, "internalipaddress", &ip) || !hueLooksLikeIp(ip)) {
    return false;
  }
  LOG("  cloud IP %s\n", ip.c_str());
  hueSetBridgeIp(ip);
  return true;
}

// Cached IP first (no mDNS round when the Bridge did not move), then mDNS, config.h, cloud.
inline bool hueFindBridge(bool skipCached = false) {
  if (gHuePairCancel) {
    return false;
  }
  hueStrLock();
  const String cached = gHueBridgeIp;
  hueStrUnlock();

  if (!skipCached && hueLooksLikeIp(cached)) {
    String id;
    if (hueProbeBridge(cached, &id)) {
      hueSetBridgeId(id);
      LOG("Bridge via cache %s\n", cached.c_str());
      return true;
    }
  }

  if (gHuePairCancel) {
    return false;
  }
  if (hueDiscoverMdns()) {
    hueStrLock();
    const String ip = gHueBridgeIp;
    hueStrUnlock();
    String id;
    if (!gHuePairCancel && hueProbeBridge(ip, &id)) {
      hueSetBridgeId(id);
      LOG("Bridge via mDNS %s id=%s\n", ip.c_str(), id.c_str());
      return true;
    }
  }

  if (gHuePairCancel) {
    return false;
  }
  if (hueLooksLikeIp(HUE_BRIDGE_IP) && cached != HUE_BRIDGE_IP) {
    String id;
    if (hueProbeBridge(HUE_BRIDGE_IP, &id)) {
      hueSetBridgeIp(String(HUE_BRIDGE_IP));
      hueSetBridgeId(id);
      LOG("Bridge via config.h %s\n", HUE_BRIDGE_IP);
      return true;
    }
  }

  if (gHuePairCancel) {
    return false;
  }
  if (hueDiscoverCloud()) {
    hueStrLock();
    const String ip = gHueBridgeIp;
    hueStrUnlock();
    String id;
    if (hueProbeBridge(ip, &id)) {
      hueSetBridgeId(id);
      LOG("Bridge via cloud %s\n", ip.c_str());
      return true;
    }
  }

  // Keep the cached IP for the next attempt.
  if (hueLooksLikeIp(cached)) {
    hueSetBridgeIp(cached);
  }
  LOGLN("Bridge not found");
  return false;
}

inline void hueBlink(unsigned long ms) {
  const bool on = ((ms / 200) % 2) == 0;
  digitalWrite(LED_BUILTIN, on ? HIGH : LOW);
}

// One pairing POST. True when the Bridge handed out a key (stored in RAM, grace armed).
inline bool huePairStep() {
  hueStrLock();
  const String ip = gHueBridgeIp;
  hueStrUnlock();
  if (!hueLooksLikeIp(ip) || gHuePairCancel) {
    return false;
  }
  hueBlink(millis());
  String body;
  const int code = hueHttpOnce("https://" + ip + "/api", "POST", "{\"devicetype\":\"hue-round-switch#xiao\"}", &body,
                               true, 4000);
  if (gHuePairCancel) {
    return false;
  }
  String user;
  if (jsonStringField(body, "username", &user) && hueLooksLikeKey(user)) {
    hueSetAppKey(user);
    // Same as Simple: 20 s so an immediate 401 does not bounce back to pairing.
    hueAuthGraceArm(20000);
    digitalWrite(LED_BUILTIN, HIGH);
    LOGLN("Paired (key stored in flash)");
    return true;
  }
  if (body.indexOf("link button not pressed") < 0 && code > 0) {
    LOG("Pair POST %d %s\n", code, body.c_str());
  }
  return false;
}

// Keyed GET on the Bridge resource. Returns the HTTP code (hueNoteAuth sees it too).
inline int hueKeyCheck() {
  hueStrLock();
  const String key = gHueAppKey;
  const String ip = gHueBridgeIp;
  hueStrUnlock();
  if (!hueLooksLikeKey(key) || !hueLooksLikeIp(ip)) {
    return -1;
  }
  String body;
  const int code = hueHttp("https://" + ip + "/clip/v2/resource/bridge", "GET", nullptr, &body, 4000);
  LOG("Hue auth GET %d\n", code);
  return code;
}
