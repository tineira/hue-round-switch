#include <WiFi.h>
#include "config.h"
#include "log.h"

#define FIRMWARE_VERSION "0.5.26"

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

static bool gWifiWasUp = false;
static bool gHueReady = false;
static unsigned long gWifiLastTryMs = 0;
static bool gSawHueAuthRejected = false;

static void applyStickyScreens();

// WiFi.SSID() está vacío hasta asociar. La red de Improv vive en la NVS de la STA.
static bool wifiHasArduinoCreds() {
  char ssid[33];
  staSavedSsid(ssid, sizeof(ssid));
  return ssid[0] != 0;
}

static void wifiBeginKnown() {
  if (wifiHasArduinoCreds()) {
    WiFi.begin();
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
    applyStickyScreens();
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

  if (!gConsoleAuthRejected) {
    uiSet(UI_LOADING);
    uiPaint();
  }
  gOnHueWait = []() {
    usbPoll();
    applyStickyScreens();
    uiTick(millis());
  };
  gOnHuePairing = [](bool pairing) {
    if (gConsoleAuthRejected) {
      return;
    }
    if (!pairing && gHueAuthRejected) {
      uiSet(UI_NO_BRIDGE);
      uiPaint();
      return;
    }
    uiSet(pairing ? UI_PAIRING : UI_LOADING);
    uiPaint();
  };

  if (!hueEnsureReady()) {
    LOGLN("Hue setup failed - press Bridge button if pairing, check Wi-Fi LAN");
    gHueReady = false;
    if (!gConsoleAuthRejected && gUi != UI_PAIRING) {
      uiSet(UI_NO_BRIDGE);
      uiPaint();
    }
    return;
  }
  gHueReady = !gHueAuthRejected;
  LOG("Using Bridge %s id=%s\n", gHueBridgeIp.c_str(), gHueBridgeId.c_str());
  recipesBindBridge(gHueBridgeId);
  pagesBindBridge(gHueBridgeId);
  consoleBootSync();
  if (gConsoleAuthRejected || gHueAuthRejected) {
    return;
  }
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
  LOG("firmware %s\n", FIRMWARE_VERSION);

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

static void gestureDrop() {
  gTouchDown = false;
  gUiPressed = false;
  gTapWaitDouble = false;
  gSecondTap = false;
  gDimDragging = false;
  gSwipeDone = false;
  gTouchMode = TOUCH_IDLE;
}

static void showPages() {
  uiSet(uiFromRecipes());
  if (gUi == UI_READY) {
    uiLoadLevel();
  }
}

// Las tareas solo ponen las banderas. Esta función, desde loop(), pinta.
// Token rechazado gana sobre No Wi-Fi, páginas, Hue error y No Bridge.
static void applyStickyScreens() {
  if (gConsoleAuthRejected) {
    gestureDrop();
    if (gHueAuthRejected) {
      gHueReady = false;
      gSawHueAuthRejected = true;
    }
    if (gUi != UI_TOKEN) {
      uiSet(UI_TOKEN);
    }
    return;
  }

  if (gHueAuthRejected) {
    gHueReady = false;
    gSawHueAuthRejected = true;
    gestureDrop();
    if (gUi != UI_PAIRING && gUi != UI_NO_BRIDGE) {
      uiSet(UI_NO_BRIDGE);
    }
    return;
  }

  if (gSawHueAuthRejected) {
    gSawHueAuthRejected = false;
    if (hueRamReady()) {
      gHueReady = true;
    }
    if (gHueReady && gUi == UI_NO_BRIDGE && !gHuePairBusy && !gHuePairReq) {
      showPages();
      return;
    }
  }

  if (gUi == UI_TOKEN) {
    gestureDrop();
    if (gWifiStaForgotten || WiFi.status() != WL_CONNECTED) {
      uiSet(UI_WIFI_FAIL);
    } else if (gHuePairBusy || gHuePairReq) {
      uiSet(UI_PAIRING);
    } else if (gHueReady) {
      showPages();
    } else {
      uiSet(UI_NO_BRIDGE);
    }
  }
}

static void usbApplyUi() {
  if (gUsbWantWifiFail) {
    gUsbWantWifiFail = false;
    gHuePairShowPending = false;
    gWifiWasUp = false;
    hueStrLock();
    gHuePairOutcome = 0;
    hueStrUnlock();
    if (!gConsoleAuthRejected && !gHueAuthRejected) {
      uiSet(UI_WIFI_FAIL);
      uiPaint();
    }
  }
  if (gHuePairShowPending && !gHuePairOutcome && gHuePairBusy && !gWifiStaForgotten) {
    gHuePairShowPending = false;
    if (!gConsoleAuthRejected) {
      uiSet(UI_PAIRING);
      uiPaint();
    }
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
    recipesBindBridge(bid);
    pagesBindBridge(bid);
    gNeedConsoleSync = true;
    gHueReady = !gHueAuthRejected;
    if (!gConsoleAuthRejected && !gHueAuthRejected) {
      uiSet(uiFromRecipes());
      uiPaint();
    }
    return;
  }
  if (gUi == UI_PAIRING) {
    if (gConsoleAuthRejected) {
      return;
    }
    if (gHueAuthRejected) {
      gHueReady = false;
      uiSet(UI_NO_BRIDGE);
      uiPaint();
      return;
    }
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
    if (!gConsoleAuthRejected && !gHueAuthRejected && !usbWifiBusy() && gUi != UI_WIFI_FAIL &&
        gUi != UI_BOOT && gUi != UI_WIFI) {
      uiSet(UI_WIFI_FAIL);
    }
    bootPoll(now);
    applyStickyScreens();
    uiTick(now);
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    gWifiWasUp = false;
    if (!gConsoleAuthRejected && !gHueAuthRejected && gUi != UI_WIFI && gUi != UI_WIFI_FAIL) {
      uiSet(UI_WIFI_FAIL);
    }
    if (!usbWifiBusy() && (now - gWifiLastTryMs >= 10000) &&
        wifiHasArduinoCreds()) {
      gWifiLastTryMs = now;
      if (!gConsoleAuthRejected && !gHueAuthRejected) {
        uiSet(UI_WIFI);
        uiPaint();
      }
      LOGLN("WiFi retry");
      WiFi.disconnect();
      wifiBeginKnown();
      wifiWait(8000);
    }
    bootPoll(now);
    consolePollTick(now);
    applyStickyScreens();
    uiTick(now);
    return;
  }

  if (!gWifiWasUp) {
    gWifiWasUp = true;
    afterWifiUp();
  }

  bootPoll(now);
  consolePollTick(now);
  applyStickyScreens();
  if (!gConsoleAuthRejected && gHueReady && gUi != UI_PAIRING && gUi != UI_TOKEN) {
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
