#pragma once

#include <ESPmDNS.h>
#include <Preferences.h>
#include "hue.h"
#include "json_util.h"

// Discover the Bridge (mDNS _hue._tcp) and pair the application key.
// IP and key are saved in NVS so a DHCP change does not require a rebuild.

static const unsigned long kPairTimeoutMs = 90000;
static const unsigned long kLongPressMs = 3000;

// The .ino hooks the screen pulse in during the pairing POST.
inline void (*gOnHueWait)() = nullptr;
inline void (*gOnHuePairing)(bool pairing) = nullptr;

inline String gHueBridgeId;

// HUEPAIR runs in the hueJob task. The USB parser reads these flags.
inline volatile bool gHuePairAsync = false;
inline volatile bool gHuePairBusy = false;
inline volatile bool gHuePairReq = false;
inline volatile bool gHuePairCancel = false;
inline volatile uint32_t gHueClrEpoch = 1;
inline volatile uint32_t gHuePairEpoch = 0;
inline volatile uint8_t gHuePairOutcome = 0;
inline volatile bool gHuePairShowPending = false;
inline volatile bool gUsbWantWifiFail = false;
inline volatile bool gWifiStaForgotten = false;

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
  gHuePairOutcome = 0;
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

inline bool hueLooksLikeKey(const String &s) {
  return s.length() >= 20 && s.indexOf("your-") < 0;
}

inline bool hueProbeBridge(const String &ip, String *bridgeId) {
  String body;
  const int code = hueHttp("https://" + ip + "/api/config", "GET", nullptr, &body, false, true);
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
  gHueAppKey = key;
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

inline bool hueDiscoverMdns() {
  if (gHuePairCancel) {
    return false;
  }
  hueStrLock();
  const String wantId = gHueBridgeId;
  hueStrUnlock();
  String host = "hue-sw-";
  host += String((uint16_t)(ESP.getEfuseMac() & 0xFFFF), HEX);
  if (!MDNS.begin(host.c_str())) {
    LOGLN("mDNS begin failed");
    return false;
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
  const int code = hueHttp("https://discovery.meethue.com/", "GET", nullptr, &body, false, false);
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

inline bool hueFindBridge() {
  if (gHuePairCancel) {
    return false;
  }
  hueStrLock();
  const String cached = gHueBridgeIp;
  hueStrUnlock();

  if (hueDiscoverMdns()) {
    if (gHuePairCancel) {
      return false;
    }
    hueStrLock();
    const String ip = gHueBridgeIp;
    hueStrUnlock();
    String id;
    if (hueProbeBridge(ip, &id)) {
      hueSetBridgeId(id);
      LOG("Bridge via mDNS %s id=%s\n", ip.c_str(), id.c_str());
      return true;
    }
  }

  if (gHuePairCancel) {
    return false;
  }
  if (hueLooksLikeIp(cached)) {
    String id;
    if (hueProbeBridge(cached, &id)) {
      hueSetBridgeIp(cached);
      hueSetBridgeId(id);
      LOG("Bridge via cache %s\n", cached.c_str());
      return true;
    }
  }

  if (gHuePairCancel) {
    return false;
  }
  if (hueLooksLikeIp(HUE_BRIDGE_IP)) {
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

  LOGLN("Bridge not found");
  return false;
}

inline void hueBlink(unsigned long ms) {
  const bool on = ((ms / 200) % 2) == 0;
  digitalWrite(LED_BUILTIN, on ? HIGH : LOW);
}

// POST /api until the Bridge button is pressed (or timeout).
inline bool huePairAppKey() {
  hueStrLock();
  const String ip = gHueBridgeIp;
  hueStrUnlock();
  if (!hueLooksLikeIp(ip)) {
    return false;
  }

  LOGLN("Pairing: press the Bridge link button");
  // In async mode the loop paints the screen. This task never touches the TFT or the CDC.
  if (!gHuePairAsync && gOnHuePairing) {
    gOnHuePairing(true);
  }
  const unsigned long start = millis();
  while (millis() - start < kPairTimeoutMs) {
    if (gHuePairCancel) {
      break;
    }
    hueBlink(millis() - start);
    String body;
    const int code = hueHttp("https://" + ip + "/api", "POST",
                             "{\"devicetype\":\"hue-round-switch#xiao\"}", &body, false, true);
    if (gHuePairCancel) {
      break;
    }
    String user;
    if (jsonStringField(body, "username", &user) && hueLooksLikeKey(user)) {
      hueSetAppKey(user);
      // Same as Simple: 20 s so an immediate 401 does not leave No Bridge stuck.
      hueAuthGraceArm(20000);
      digitalWrite(LED_BUILTIN, HIGH);
      LOGLN("Paired (key stored in flash)");
      if (!gHuePairAsync && gOnHuePairing) {
        gOnHuePairing(false);
      }
      return true;
    }
    if (body.indexOf("link button not pressed") < 0 && code > 0) {
      LOG("Pair POST %d %s\n", code, body.c_str());
    }
    if (!gHuePairAsync && gOnHueWait) {
      gOnHueWait();
    }
    for (uint8_t i = 0; i < 8 && !gHuePairCancel; i++) {
      delay(50);
    }
  }
  digitalWrite(LED_BUILTIN, LOW);
  if (gHuePairCancel) {
    LOGLN("Pairing cancelled");
  } else {
    LOGLN("Pairing timeout");
  }
  if (!gHuePairAsync && gOnHuePairing) {
    gOnHuePairing(false);
  }
  return false;
}

inline bool hueKeyWorks() {
  hueStrLock();
  const String key = gHueAppKey;
  const String ip = gHueBridgeIp;
  hueStrUnlock();
  if (!hueLooksLikeKey(key) || !hueLooksLikeIp(ip)) {
    return false;
  }
  String body;
  const int code = hueHttp("https://" + ip + "/clip/v2/resource/bridge", "GET", nullptr, &body, true, true);
  LOG("Hue auth GET %d\n", code);
  return code == HTTP_CODE_OK;
}

inline bool hueEnsureReady() {
  hueLoadStore();
  if (!hueFindBridge()) {
    return false;
  }
  if (!hueKeyWorks()) {
    if (!huePairAppKey() || !hueKeyWorks()) {
      return false;
    }
  }
  hueSaveStore();
  return true;
}

inline bool hueRePair() {
  LOGLN("Re-pair requested");
  const bool ownBusy = !gHuePairBusy;
  if (ownBusy) {
    gHuePairBusy = true;
  }
  hueSetAppKey("");
  bool ok = false;
  if (!gHuePairCancel && hueFindBridge() && !gHuePairCancel && huePairAppKey() && !gHuePairCancel) {
    hueSaveStore();
    hueKeyWorks();
    // The POST already delivered the key. A failed check GET does not send the screen back to "no Bridge".
    ok = !gHuePairCancel && hueLooksLikeKey(gHueAppKey);
  }
  if (gHuePairCancel) {
    ok = false;
  }
  if (ownBusy) {
    gHuePairBusy = false;
  }
  return ok;
}
