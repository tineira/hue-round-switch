#pragma once

#include <WiFi.h>
#include <esp_wifi.h>
#include <string.h>
#include "console.h"
#include "hue_job.h"

// Improv Serial + HUESET/HUEGET/HUEPAIR/HUECLR on USB CDC. Logs stay behind SERIAL_DEBUG so they
// do not mix with binary Improv packets in the product binary.

static const uint8_t kImprovMagic[6] = {'I', 'M', 'P', 'R', 'O', 'V'};
static const uint8_t kImprovVer = 1;
static const unsigned long kImprovByteMs = 500;
static const unsigned long kImprovConnectMs = 30000UL;

enum {
  IMPROV_PKT_STATE = 0x01,
  IMPROV_PKT_ERROR = 0x02,
  IMPROV_PKT_RPC = 0x03,
  IMPROV_PKT_RPC_RESULT = 0x04,
};

enum {
  IMPROV_ST_READY = 0x02,
  IMPROV_ST_PROVISIONING = 0x03,
  IMPROV_ST_PROVISIONED = 0x04,
};

enum {
  IMPROV_CMD_WIFI = 0x01,
  IMPROV_CMD_STATE = 0x02,
  IMPROV_CMD_INFO = 0x03,
  IMPROV_CMD_SCAN = 0x04,
};

enum {
  IMPROV_ERR_NONE = 0x00,
  IMPROV_ERR_INVALID = 0x01,
  IMPROV_ERR_UNKNOWN = 0x02,
  IMPROV_ERR_CONNECT = 0x03,
};

enum UsbParse { USB_IDLE = 0, USB_IMPROV, USB_LINE };

inline uint8_t gImprovRx[256];
inline uint16_t gImprovRxLen = 0;
inline unsigned long gImprovRxMs = 0;
inline UsbParse gUsbParse = USB_IDLE;
inline char gUsbLine[192];
inline uint8_t gUsbLineLen = 0;
inline bool gImprovConnecting = false;
inline unsigned long gImprovConnectAt = 0;
inline bool gImprovScanPending = false;
inline bool gImprovScanStarted = false;
inline bool gImprovScanDefer = false;
inline unsigned long gImprovScanAt = 0;
inline unsigned long gImprovScanKickAt = 0;

inline bool usbWifiBusy() { return gImprovConnecting || gImprovScanPending; }

inline void usbReply(const char *line) {
  Serial.print(line);
  Serial.print('\n');
}

inline void improvSend(uint8_t type, const uint8_t *data, uint8_t len) {
  uint8_t pkt[256];
  if ((uint16_t)len + 11 > sizeof(pkt)) {
    return;
  }
  memcpy(pkt, kImprovMagic, 6);
  pkt[6] = kImprovVer;
  pkt[7] = type;
  pkt[8] = len;
  if (len && data) {
    memcpy(pkt + 9, data, len);
  }
  uint16_t sum = 0;
  const uint16_t n = 9 + len;
  for (uint16_t i = 0; i < n; i++) {
    sum = (uint16_t)(sum + pkt[i]);
  }
  pkt[n] = (uint8_t)(sum & 0xFF);
  pkt[n + 1] = '\n';
  // No Serial.flush(): on HWCDC it can hang TX until the host reads.
  Serial.write(pkt, n + 2);
}

inline void improvSendState(uint8_t state) { improvSend(IMPROV_PKT_STATE, &state, 1); }

inline void improvSendError(uint8_t err) { improvSend(IMPROV_PKT_ERROR, &err, 1); }

inline void improvSendRpcStrings(uint8_t cmd, const char *const *strs, uint8_t n) {
  uint8_t data[240];
  uint16_t pos = 2;
  data[0] = cmd;
  for (uint8_t i = 0; i < n; i++) {
    const char *s = strs && strs[i] ? strs[i] : "";
    size_t sl = strlen(s);
    if (sl > 255) {
      sl = 255;
    }
    if (pos + 1 + sl > sizeof(data)) {
      break;
    }
    data[pos++] = (uint8_t)sl;
    memcpy(data + pos, s, sl);
    pos = (uint16_t)(pos + sl);
  }
  data[1] = (uint8_t)(pos - 2);
  improvSend(IMPROV_PKT_RPC_RESULT, data, (uint8_t)pos);
}

inline uint8_t improvCurrentState() {
  if (gImprovConnecting) {
    return IMPROV_ST_PROVISIONING;
  }
  if (WiFi.status() == WL_CONNECTED) {
    return IMPROV_ST_PROVISIONED;
  }
  return IMPROV_ST_READY;
}

// Spec: Current State is written to listening clients (no RPC required).
inline void improvHello() { improvSendState(improvCurrentState()); }

inline void improvSendInfo() {
  const char *strs[4] = {
      "hue-round-switch",
      FIRMWARE_VERSION,
      "XIAO_ESP32S3/esp32-s3",
      "Round Display",
  };
  improvSendRpcStrings(IMPROV_CMD_INFO, strs, 4);
}

inline void improvStartScan() {
  if (gImprovConnecting) {
    improvSendState(improvCurrentState());
    improvSendRpcStrings(IMPROV_CMD_SCAN, nullptr, 0);
    return;
  }
  // Acknowledge the state now: scanNetworks/mode can block the CDC and the wizard
  // sees 4 s of silence. The scan starts on the next usbPoll.
  gImprovScanPending = true;
  gImprovScanStarted = false;
  gImprovScanDefer = true;
  gImprovScanAt = millis();
  improvSendState(improvCurrentState());
}

// Start the async scan. STA may be in WiFi.begin() since boot: the attempt
// must be cut (without erasing NVS) or scanNetworks keeps failing.
inline void improvKickScan() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.disconnect(false, false);
  WiFi.scanDelete();
  WiFi.scanNetworks(true);
  gImprovScanKickAt = millis();
}

inline void improvBeginScan() {
  if (!gImprovScanPending || gImprovScanStarted) {
    return;
  }
  gImprovScanStarted = true;
  gImprovScanAt = millis();
  improvKickScan();
}

inline void improvFlushScan() {
  if (!gImprovScanStarted) {
    return;
  }
  const int16_t n = WiFi.scanComplete();
  const unsigned long elapsed = millis() - gImprovScanAt;
  if (n == WIFI_SCAN_RUNNING) {
    return;
  }
  // FAILED (or 0 too early): retry every ~400 ms for up to 15 s, never sitting idle.
  if ((n == WIFI_SCAN_FAILED || (n == 0 && elapsed < 3000UL)) && elapsed < 15000UL) {
    if (millis() - gImprovScanKickAt >= 400UL) {
      improvKickScan();
    }
    return;
  }
  gImprovScanPending = false;
  gImprovScanStarted = false;
  gImprovScanDefer = false;
  if (n > 0) {
    for (int16_t i = 0; i < n; i++) {
      char rssi[8];
      snprintf(rssi, sizeof(rssi), "%d", WiFi.RSSI(i));
      char ssid[33];
      strlcpy(ssid, WiFi.SSID(i).c_str(), sizeof(ssid));
      const char *auth = WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "NO" : "YES";
      const char *strs[3] = {ssid, rssi, auth};
      improvSendRpcStrings(IMPROV_CMD_SCAN, strs, 3);
    }
  }
  improvSendRpcStrings(IMPROV_CMD_SCAN, nullptr, 0);
  WiFi.scanDelete();
}

inline void improvPollConnect() {
  if (!gImprovConnecting) {
    return;
  }
  if (WiFi.status() == WL_CONNECTED) {
    gImprovConnecting = false;
    improvSendState(IMPROV_ST_PROVISIONED);
    improvSendRpcStrings(IMPROV_CMD_WIFI, nullptr, 0);
    return;
  }
  if (millis() - gImprovConnectAt >= kImprovConnectMs) {
    gImprovConnecting = false;
    improvSendError(IMPROV_ERR_CONNECT);
    improvSendState(IMPROV_ST_READY);
  }
}

inline void improvOnWifi(const uint8_t *p, uint8_t inner) {
  if (inner < 2) {
    improvSendError(IMPROV_ERR_INVALID);
    return;
  }
  const uint8_t ssidLen = p[0];
  if (ssidLen == 0 || ssidLen > 32 || (uint16_t)1 + ssidLen + 1 > inner) {
    improvSendError(IMPROV_ERR_INVALID);
    return;
  }
  const uint8_t passLen = p[1 + ssidLen];
  if (passLen > 64 || (uint16_t)1 + ssidLen + 1 + passLen > inner) {
    improvSendError(IMPROV_ERR_INVALID);
    return;
  }
  char ssid[33];
  char pass[65];
  memcpy(ssid, p + 1, ssidLen);
  ssid[ssidLen] = 0;
  memcpy(pass, p + 2 + ssidLen, passLen);
  pass[passLen] = 0;

  gImprovScanPending = false;
  gImprovScanStarted = false;
  gImprovScanDefer = false;
  WiFi.scanDelete();
  WiFi.persistent(true);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.disconnect(false, false);
  gWifiStaForgotten = false;
  WiFi.begin(ssid, pass);
  gImprovConnecting = true;
  gImprovConnectAt = millis();
  improvSendState(IMPROV_ST_PROVISIONING);
}

inline void improvOnRpc(const uint8_t *data, uint8_t len) {
  if (len < 2) {
    improvSendError(IMPROV_ERR_INVALID);
    return;
  }
  const uint8_t cmd = data[0];
  const uint8_t inner = data[1];
  if ((uint16_t)inner + 2 > len) {
    improvSendError(IMPROV_ERR_INVALID);
    return;
  }
  improvSendError(IMPROV_ERR_NONE);
  switch (cmd) {
    case IMPROV_CMD_WIFI:
      improvOnWifi(data + 2, inner);
      break;
    case IMPROV_CMD_STATE:
      improvSendState(improvCurrentState());
      if (WiFi.status() == WL_CONNECTED) {
        improvSendRpcStrings(IMPROV_CMD_WIFI, nullptr, 0);
      }
      break;
    case IMPROV_CMD_INFO:
      improvSendInfo();
      break;
    case IMPROV_CMD_SCAN:
      improvStartScan();
      break;
    default:
      improvSendError(IMPROV_ERR_UNKNOWN);
      break;
  }
}

inline void usbResetParse() {
  gUsbParse = USB_IDLE;
  gImprovRxLen = 0;
  gUsbLineLen = 0;
}

inline bool usbAppendRaw(char *dst, size_t cap, size_t *len, const char *s) {
  if (!s) {
    s = "";
  }
  const size_t n = strlen(s);
  if (*len + n >= cap) {
    return false;
  }
  memcpy(dst + *len, s, n);
  *len += n;
  return true;
}

inline bool usbAppendPct(char *dst, size_t cap, size_t *len, const char *val) {
  if (!val) {
    val = "";
  }
  static const char kHex[] = "0123456789ABCDEF";
  for (const unsigned char *p = reinterpret_cast<const unsigned char *>(val); *p; p++) {
    const unsigned char c = *p;
    const bool raw = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
                     c == '.' || c == '_' || c == '~';
    if (raw) {
      if (*len + 1 >= cap) {
        return false;
      }
      dst[(*len)++] = static_cast<char>(c);
    } else {
      if (*len + 3 >= cap) {
        return false;
      }
      dst[(*len)++] = '%';
      dst[(*len)++] = kHex[c >> 4];
      dst[(*len)++] = kHex[c & 0x0F];
    }
  }
  return true;
}

inline bool usbAppendField(char *dst, size_t cap, size_t *len, bool *first, const char *key, const char *val) {
  if (!*first && !usbAppendRaw(dst, cap, len, " ")) {
    return false;
  }
  *first = false;
  if (!usbAppendRaw(dst, cap, len, key) || !usbAppendRaw(dst, cap, len, "=")) {
    return false;
  }
  return usbAppendPct(dst, cap, len, val);
}

inline void nvsCopyStr(Preferences &prefs, const char *key, char *out, size_t cap) {
  out[0] = 0;
  if (!cap) {
    return;
  }
  const String v = prefs.getString(key, "");
  size_t n = v.length();
  if (n >= cap) {
    n = cap - 1;
  }
  memcpy(out, v.c_str(), n);
  out[n] = 0;
}

// SSID saved in the STA (driver NVS), even with the radio down. Not the one from config.h.
inline void staSavedSsid(char *out, size_t cap) {
  out[0] = 0;
  if (!cap) {
    return;
  }
  wifi_config_t conf;
  memset(&conf, 0, sizeof(conf));
  if (esp_wifi_get_config(WIFI_IF_STA, &conf) != ESP_OK) {
    return;
  }
  size_t n = strnlen(reinterpret_cast<const char *>(conf.sta.ssid), sizeof(conf.sta.ssid));
  if (n >= cap) {
    n = cap - 1;
  }
  memcpy(out, conf.sta.ssid, n);
  out[n] = 0;
}

inline void wifiForgetSta() {
  gWifiStaForgotten = true;
  gImprovConnecting = false;
  gImprovScanPending = false;
  gImprovScanStarted = false;
  gImprovScanDefer = false;
  WiFi.scanDelete();
  WiFi.persistent(true);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  esp_wifi_disconnect();
  wifi_config_t conf;
  memset(&conf, 0, sizeof(conf));
  esp_wifi_set_config(WIFI_IF_STA, &conf);
}

inline void wifiKeepForgotten() {
  if (!gWifiStaForgotten || usbWifiBusy()) {
    return;
  }
  if (WiFi.status() == WL_CONNECTED) {
    esp_wifi_disconnect();
  }
}

inline void usbReplySta() {
  char mac[13];
  strlcpy(mac, deviceMacHex().c_str(), sizeof(mac));
  char ssid[33];
  staSavedSsid(ssid, sizeof(ssid));
  const bool up = WiFi.status() == WL_CONNECTED;
  char ip[16];
  ip[0] = 0;
  if (up) {
    strlcpy(ip, WiFi.localIP().toString().c_str(), sizeof(ip));
  }
  char bid[80];
  char bip[48];
  char url[kConsoleUrlMax];
  char key[128];
  char tok[kConsoleTokMax];
  bid[0] = 0;
  bip[0] = 0;
  url[0] = 0;
  key[0] = 0;
  tok[0] = 0;
  Preferences huePrefs;
  if (huePrefs.begin("hue", true)) {
    nvsCopyStr(huePrefs, "bid", bid, sizeof(bid));
    nvsCopyStr(huePrefs, "ip", bip, sizeof(bip));
    nvsCopyStr(huePrefs, "key", key, sizeof(key));
    huePrefs.end();
  }
  Preferences conPrefs;
  if (conPrefs.begin("console", true)) {
    nvsCopyStr(conPrefs, "url", url, sizeof(url));
    nvsCopyStr(conPrefs, "token", tok, sizeof(tok));
    conPrefs.end();
  }
  const char *tokenFlag = consoleLooksLikeToken(tok) ? "1" : "0";
  const char *keyFlag = hueLooksLikeKey(String(key)) ? "1" : "0";

  static char line[1280];
  size_t len = 0;
  bool first = true;
  bool ok = usbAppendRaw(line, sizeof(line), &len, "HUESTA ");
  ok = ok && usbAppendField(line, sizeof(line), &len, &first, "mac", mac);
  ok = ok && usbAppendField(line, sizeof(line), &len, &first, "product", "round");
  ok = ok && usbAppendField(line, sizeof(line), &len, &first, "ver", FIRMWARE_VERSION);
  ok = ok && usbAppendField(line, sizeof(line), &len, &first, "chip", "s3");
  ok = ok && usbAppendField(line, sizeof(line), &len, &first, "ssid", ssid);
  ok = ok && usbAppendField(line, sizeof(line), &len, &first, "wifi", up ? "up" : "down");
  ok = ok && usbAppendField(line, sizeof(line), &len, &first, "ip", up ? ip : "");
  ok = ok && usbAppendField(line, sizeof(line), &len, &first, "bid", bid);
  ok = ok && usbAppendField(line, sizeof(line), &len, &first, "bip", bip);
  ok = ok && usbAppendField(line, sizeof(line), &len, &first, "url", url);
  ok = ok && usbAppendField(line, sizeof(line), &len, &first, "token", tokenFlag);
  ok = ok && usbAppendField(line, sizeof(line), &len, &first, "key", keyFlag);
  if (!ok || len + 1 > sizeof(line)) {
    usbReply("HUEERR unknown");
    return;
  }
  line[len] = 0;
  usbReply(line);
}

inline void usbCmdPair() {
  if (WiFi.status() != WL_CONNECTED) {
    usbReply("HUEERR no-wifi");
    return;
  }
  if (huePairBusy()) {
    usbReply("HUEOK pair");
    return;
  }
  if (!huePairRequest()) {
    usbReply("HUEERR unknown");
    return;
  }
  usbReply("HUEOK pair");
}

inline void usbCmdClear() {
  // Cancel first (the hueJob task aborts pairing / saving), wipe, then bump the epoch
  // so the task restarts only once RAM and NVS are empty.
  gHuePairCancel = true;
  wifiForgetSta();
  consoleForget();
  recipesForgetSaved();
  pagesForgetSaved();
  hueForgetSaved();
  gHueClrEpoch++;
  if (gHueClrEpoch == 0) {
    gHueClrEpoch = 1;
  }
  hueJobNotify();
  usbReply("HUEOK clear");
}

inline void usbHandleLine() {
  gUsbLine[gUsbLineLen] = 0;
  char *line = gUsbLine;
  while (*line == ' ' || *line == '\t') {
    line++;
  }
  size_t n = strlen(line);
  while (n && (line[n - 1] == ' ' || line[n - 1] == '\t' || line[n - 1] == '\r')) {
    line[--n] = 0;
  }
  if (!n) {
    return;
  }
  if (strcmp(line, "HUEGET") == 0) {
    usbReplySta();
    return;
  }
  if (strcmp(line, "HUEPAIR") == 0) {
    usbCmdPair();
    return;
  }
  if (strcmp(line, "HUECLR") == 0) {
    usbCmdClear();
    return;
  }
  if (strcmp(line, "HUESET") == 0) {
    usbReply("HUEERR missing value");
    return;
  }
  if (strncmp(line, "HUESET ", 7) != 0) {
    usbReply("HUEERR unknown");
    return;
  }
  char *rest = line + 7;
  while (*rest == ' ') {
    rest++;
  }
  char *sp = strchr(rest, ' ');
  if (!sp || !sp[1]) {
    usbReply("HUEERR missing value");
    return;
  }
  *sp = 0;
  const char *key = rest;
  char *val = sp + 1;
  while (*val == ' ') {
    val++;
  }
  if (strcmp(key, "token") == 0) {
    if (!consoleSetToken(val)) {
      usbReply("HUEERR token");
      return;
    }
    usbReply("HUEOK token");
    gNeedConsoleSync = true;
  } else if (strcmp(key, "url") == 0) {
    if (!consoleSetUrl(val)) {
      usbReply("HUEERR url");
      return;
    }
    usbReply("HUEOK url");
    gNeedConsoleSync = true;
  } else {
    usbReply("HUEERR unknown");
  }
}

inline void usbOnImprovByte(uint8_t b) {
  if (gImprovRxLen >= sizeof(gImprovRx)) {
    usbResetParse();
    return;
  }
  gImprovRx[gImprovRxLen++] = b;
  gImprovRxMs = millis();

  if (gImprovRxLen <= 6) {
    if (gImprovRx[gImprovRxLen - 1] != kImprovMagic[gImprovRxLen - 1]) {
      usbResetParse();
    }
    return;
  }
  if (gImprovRxLen < 9) {
    return;
  }
  if (gImprovRx[6] != kImprovVer) {
    usbResetParse();
    return;
  }
  const uint8_t dlen = gImprovRx[8];
  const uint16_t need = (uint16_t)9 + dlen + 1;
  if (gImprovRxLen < need) {
    return;
  }
  uint16_t sum = 0;
  for (uint16_t i = 0; i < need - 1; i++) {
    sum = (uint16_t)(sum + gImprovRx[i]);
  }
  if ((uint8_t)(sum & 0xFF) != gImprovRx[need - 1]) {
    improvSendError(IMPROV_ERR_INVALID);
    usbResetParse();
    return;
  }
  if (gImprovRx[7] == IMPROV_PKT_RPC) {
    improvOnRpc(gImprovRx + 9, dlen);
  }
  gUsbParse = USB_IDLE;
  gImprovRxLen = 0;
}

inline void usbOnByte(uint8_t b) {
  if (gUsbParse == USB_IMPROV) {
    usbOnImprovByte(b);
    return;
  }
  if (gUsbParse == USB_LINE) {
    if (b == '\n') {
      usbHandleLine();
      usbResetParse();
      return;
    }
    if (b == '\r') {
      return;
    }
    if (gUsbLineLen + 1 < sizeof(gUsbLine)) {
      gUsbLine[gUsbLineLen++] = (char)b;
    } else {
      usbResetParse();
    }
    return;
  }
  if (b == 'I') {
    gUsbParse = USB_IMPROV;
    gImprovRxLen = 0;
    usbOnImprovByte(b);
    return;
  }
  if (b == '\n' || b == '\r') {
    return;
  }
  gUsbParse = USB_LINE;
  gUsbLineLen = 0;
  if (gUsbLineLen + 1 < sizeof(gUsbLine)) {
    gUsbLine[gUsbLineLen++] = (char)b;
  }
}

inline void usbPoll() {
  if (gUsbParse == USB_IMPROV && gImprovRxLen > 0 && (millis() - gImprovRxMs) > kImprovByteMs) {
    usbResetParse();
  }
  while (Serial.available() > 0) {
    usbOnByte((uint8_t)Serial.read());
  }

  if (gImprovScanPending) {
    if (gImprovScanDefer) {
      gImprovScanDefer = false;
    } else {
      improvBeginScan();
      improvFlushScan();
    }
  }
  improvPollConnect();
}
