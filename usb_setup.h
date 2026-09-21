#pragma once

#include <WiFi.h>
#include <string.h>
#include "console.h"

// Improv Serial + HUESET on USB CDC. Logs stay behind SERIAL_DEBUG so they
// do not mix with binary Improv packets in the product binary.

static const uint8_t kImprovMagic[6] = {'I', 'M', 'P', 'R', 'O', 'V'};
static const uint8_t kImprovVer = 1;
static const unsigned long kImprovByteMs = 100;
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
inline bool gImprovScan = false;

inline bool usbWifiBusy() { return gImprovConnecting; }

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
  WiFi.mode(WIFI_STA);
  WiFi.scanDelete();
  WiFi.scanNetworks(true);
  gImprovScan = true;
}

inline void improvPollScan() {
  if (!gImprovScan) {
    return;
  }
  const int16_t n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) {
    return;
  }
  gImprovScan = false;
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

  WiFi.persistent(true);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.disconnect(false, false);
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
  if (strncmp(line, "HUESET ", 7) != 0) {
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
  improvPollScan();
  improvPollConnect();
}
