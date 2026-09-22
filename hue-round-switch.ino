#include <WiFi.h>
#include "config.h"
#include "log.h"

#define FIRMWARE_VERSION "0.5.23"

SET_LOOP_TASK_STACK_SIZE(24576);

String gHueBridgeIp;
String gHueAppKey;

#include "hue.h"
#include "hue_discover.h"
#include "pages.h"
#include "recipes.h"
#include "display.h"
#include "touch.h"
#include "channels.h"
#include "ui.h"
#include "console.h"
#include "usb_setup.h"

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

static bool gWifiWasUp = false;
static bool gHueReady = false;
static unsigned long gWifiLastTryMs = 0;

static bool wifiHasArduinoCreds() { return WiFi.SSID().length() > 0; }

static bool wifiHasDevSsid() { return WIFI_SSID[0] != '\0'; }

static void wifiBeginKnown() {
  if (wifiHasArduinoCreds()) {
    WiFi.begin();
  } else if (wifiHasDevSsid()) {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
}

static void usbPump(unsigned long ms) {
  const unsigned long start = millis();
  usbPoll();
  while (millis() - start < ms) {
    delay(10);
    usbPoll();
  }
}

static bool wifiWait(unsigned long maxMs) {
  const unsigned long start = millis();
  LOGS("WiFi");
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < maxMs) {
    usbPoll();
    uiTick(millis());
    if (usbWifiBusy()) {
      LOGLN("");
      return false;
    }
    delay(10);
    LOGS(".");
  }
  LOGLN("");
  return WiFi.status() == WL_CONNECTED;
}

static void afterWifiUp() {
  LOG("IP: %s\n", WiFi.localIP().toString().c_str());
  LOG("mac %s\n", deviceMacHex().c_str());
  digitalWrite(LED_BUILTIN, HIGH);

  uiSet(UI_LOADING);
  uiPaint();
  gOnHueWait = []() {
    usbPoll();
    uiTick(millis());
  };
  gOnHuePairing = [](bool pairing) {
    uiSet(pairing ? UI_PAIRING : UI_LOADING);
    uiPaint();
  };

  if (!hueEnsureReady()) {
    LOGLN("Hue setup failed - press Bridge button if pairing, check Wi-Fi LAN");
    uiSet(UI_NO_BRIDGE);
    uiPaint();
    gHueReady = false;
    return;
  }
  gHueReady = true;
  LOG("Using Bridge %s id=%s\n", gHueBridgeIp.c_str(), gHueBridgeId.c_str());
  recipesBindBridge(gHueBridgeId);
  pagesBindBridge(gHueBridgeId);
  consoleBootSync();
  uiSet(uiFromRecipes());
  if (gUi == UI_READY) {
    uiLoadLevel();
  }
  uiPaint();
  LOGLN("tap/double = recipe. swipe = page. ring = dim. BOOT hold 3s = re-pair.");
}

void setup() {
  Serial.begin(115200);
  // USB CDC: sin PC el write() espera al host. 100 ms: 0 descartaba escrituras si tx_lock estaba ocupado.
  Serial.setTxTimeoutMs(100);
  improvHello();
  usbPoll();
  usbPump(200);
  improvHello();

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  LOGLN("hue-round-switch");
  LOG("firmware %s  SSID: %s\n", FIRMWARE_VERSION, WIFI_SSID);

  consoleLoadNvs();
  recipesLoad();
  pagesLoad();
  bootBegin();
  usbPoll();
  displayBegin();
  touchBegin();
  hueJobBegin();
  consoleJobBegin();
  usbPoll();

  uiSet(UI_BOOT);
  uiPaint();
  usbPump(1000);

  uiSet(UI_WIFI);
  uiPaint();

  WiFi.persistent(true);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  gWifiLastTryMs = millis();

  // Web Serial DTR-resets the S3; Scan/ping can arrive during the splash.
  // Do not WiFi.begin over an Improv scan, and keep usbPoll alive if STA fails.
  if (usbWifiBusy()) {
    uiSet(UI_WIFI_FAIL);
    uiPaint();
    return;
  }

  if (wifiHasArduinoCreds()) {
    WiFi.begin();
  } else if (wifiHasDevSsid()) {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  } else {
    uiSet(UI_WIFI_FAIL);
    uiPaint();
    return;
  }

  if (!wifiWait(15000)) {
    if (!usbWifiBusy()) {
      LOG("WiFi failed, status=%d (S3 needs the U.FL antenna)\n", (int)WiFi.status());
    }
    uiSet(UI_WIFI_FAIL);
    uiPaint();
    return;
  }

  gWifiWasUp = true;
  afterWifiUp();
}

static void usbApplyUi() {
  if (gUsbWantWifiFail) {
    gUsbWantWifiFail = false;
    gHuePairShowPending = false;
    gWifiWasUp = false;
    hueStrLock();
    gHuePairOutcome = 0;
    hueStrUnlock();
    uiSet(UI_WIFI_FAIL);
    uiPaint();
  }
  if (gHuePairShowPending && !gHuePairOutcome && gHuePairBusy && !gWifiStaForgotten) {
    gHuePairShowPending = false;
    uiSet(UI_PAIRING);
    uiPaint();
  } else {
    gHuePairShowPending = false;
  }
  if (!gHuePairOutcome) {
    return;
  }
  hueStrLock();
  const uint8_t outcome = gHuePairOutcome;
  gHuePairOutcome = 0;
  const bool fresh = !gHuePairCancel && gHuePairEpoch == gHueClrEpoch;
  const String bid = gHueBridgeId;
  hueStrUnlock();
  if (!fresh) {
    return;
  }
  if (outcome == 1) {
    gHueReady = true;
    recipesBindBridge(bid);
    pagesBindBridge(bid);
    gNeedConsoleSync = true;
    uiSet(uiFromRecipes());
    uiPaint();
    return;
  }
  if (gUi == UI_PAIRING) {
    uiSet(gHueReady ? uiFromRecipes() : UI_NO_BRIDGE);
    uiPaint();
  }
}

void loop() {
  const unsigned long now = millis();
  usbPoll();
  usbApplyUi();

  if (gWifiStaForgotten) {
    wifiKeepForgotten();
    gWifiWasUp = false;
    if (!usbWifiBusy() && gUi != UI_WIFI_FAIL && gUi != UI_BOOT && gUi != UI_WIFI) {
      uiSet(UI_WIFI_FAIL);
    }
    bootPoll(now);
    uiTick(now);
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    gWifiWasUp = false;
    if (gUi != UI_WIFI && gUi != UI_WIFI_FAIL) {
      uiSet(UI_WIFI_FAIL);
    }
    if (!usbWifiBusy() && (now - gWifiLastTryMs >= 10000) &&
        (wifiHasArduinoCreds() || wifiHasDevSsid())) {
      gWifiLastTryMs = now;
      uiSet(UI_WIFI);
      uiPaint();
      LOGLN("WiFi retry");
      WiFi.disconnect();
      wifiBeginKnown();
      wifiWait(8000);
    }
    bootPoll(now);
    consolePollTick(now);
    uiTick(now);
    return;
  }

  if (!gWifiWasUp) {
    gWifiWasUp = true;
    afterWifiUp();
  }

  bootPoll(now);
  consolePollTick(now);
  if (gHueReady && gUi != UI_PAIRING) {
    uiPollTouch(now);
    if (gUi == UI_EMPTY || gUi == UI_READY || gUi == UI_ERROR) {
      const UiScreen next = uiFromRecipes();
      if (gUi != UI_ERROR && next != gUi) {
        uiSet(next);
      }
    }
  }
  uiTick(now);
}
