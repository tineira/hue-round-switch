#include <WiFi.h>
#include "config.h"
#include "log.h"

#define FIRMWARE_VERSION "0.5.27"

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

// Ownership (see docs/specs/stability.md §4.9):
// - loop (core 1): UI, gestures, pages / recipes in RAM and their NVS.
// - hueJob task: the Bridge connection, the link state, Bridge IP / key / id.
// - consoleJob task: the Vercel connection; the config body reaches the loop via a queue.

static const unsigned long kWifiConnectingMs = 15000;  // "Wi-Fi..." before "No Wi-Fi" at boot
static const unsigned long kWifiRetryMs = 30000;       // Let auto-reconnect work first
static const unsigned long kWifiDropGraceMs = 5000;    // A short drop keeps Ready on screen

static bool gWifiWasUp = false;
static bool gWifiEverUp = false;
static unsigned long gWifiBeginMs = 0;
static unsigned long gWifiDownMs = 0;
static bool gConsoleBooted = false;
static String gBoundBridgeId;
static bool gTokenMarkShown = false;

// WiFi.SSID() is empty until associated. The Improv network lives in the STA's NVS.
static bool wifiHasArduinoCreds() {
  char ssid[33];
  staSavedSsid(ssid, sizeof(ssid));
  return ssid[0] != 0;
}

static void wifiBeginKnown() {
  gWifiBeginMs = millis();
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

void setup() {
  Serial.begin(115200);
  // USB CDC: without a PC, write() waits for the host. 100 ms: 0 dropped writes when tx_lock was busy.
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
  hueLoadStore();
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

  WiFi.persistent(true);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);

  // Web Serial DTR-resets the S3; Scan/ping can arrive during the splash.
  // Do not WiFi.begin over an Improv scan.
  if (!usbWifiBusy()) {
    wifiBeginKnown();
  }
  uiSet(wifiHasArduinoCreds() ? UI_WIFI : UI_WIFI_FAIL);
  uiPaint();
  LOGLN("tap/double = recipe. swipe = page. ring = dim. BOOT hold 3s = re-pair.");
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

// Never blocks: the STA reconnects by itself; begin() again only after kWifiRetryMs.
static void wifiTick(unsigned long now) {
  if (gWifiStaForgotten) {
    wifiKeepForgotten();
  }
  const bool up = !gWifiStaForgotten && WiFi.status() == WL_CONNECTED;
  if (up) {
    if (!gWifiWasUp) {
      gWifiWasUp = true;
      gWifiEverUp = true;
      LOG("IP: %s\n", WiFi.localIP().toString().c_str());
      LOG("mac %s\n", deviceMacHex().c_str());
      digitalWrite(LED_BUILTIN, HIGH);
      gHueWifiEpoch++;
      hueJobNotify();
    }
    return;
  }
  if (gWifiWasUp) {
    gWifiWasUp = false;
    gWifiDownMs = now;
    digitalWrite(LED_BUILTIN, LOW);
    LOGLN("WiFi down");
  }
  if (!gWifiStaForgotten && !usbWifiBusy() && now - gWifiBeginMs >= kWifiRetryMs && wifiHasArduinoCreds()) {
    LOGLN("WiFi retry");
    WiFi.disconnect();
    wifiBeginKnown();
  }
}

// The hueJob task reached READY through setup or pairing: bind pages / recipes to the
// Bridge and sync the console.
static void hueBindTick() {
  if (!gHueBindPending) {
    return;
  }
  gHueBindPending = false;
  const bool paired = gHueBindPaired.exchange(false);
  hueStrLock();
  const String bid = gHueBridgeId;
  const String ip = gHueBridgeIp;
  hueStrUnlock();
  LOG("Using Bridge %s id=%s\n", ip.c_str(), bid.c_str());
  recipesBindBridge(bid);
  pagesBindBridge(bid);
  const bool bridgeChanged = gBoundBridgeId.length() && bid.length() && !gBoundBridgeId.equalsIgnoreCase(bid);
  if (bid.length()) {
    gBoundBridgeId = bid;
  }
  if (!gConsoleBooted) {
    gConsoleBooted = true;
    consoleBootSync();
  } else if (paired || bridgeChanged) {
    gNeedConsoleSync = true;
  }
  gNeedHueState = true;
  gNeedFullPaint = true;
}

// One place picks the screen. Tasks only publish state.
static void applyScreen(unsigned long now) {
  UiScreen want = gUi;
  if (gConsoleAuthRejected && gRecipeCount == 0) {
    // Nothing to run locally: the token is the problem to show.
    want = UI_TOKEN;
  } else if (gWifiStaForgotten || WiFi.status() != WL_CONNECTED) {
    if (!gWifiEverUp || gWifiStaForgotten) {
      const bool connecting = !gWifiStaForgotten && (now - gWifiBeginMs) < kWifiConnectingMs && wifiHasArduinoCreds();
      want = connecting ? UI_WIFI : UI_WIFI_FAIL;
    } else if (now - gWifiDownMs >= kWifiDropGraceMs) {
      want = UI_WIFI_FAIL;
    } else if (gUi != UI_READY && gUi != UI_EMPTY) {
      want = UI_WIFI_FAIL;
    }
  } else {
    switch (gHueLink.load()) {
      case LINK_START:
        want = UI_LOADING;
        break;
      case LINK_SEARCHING:
        want = UI_NO_BRIDGE;
        break;
      case LINK_PAIRING:
        want = UI_PAIRING;
        break;
      default:
        want = uiFromRecipes();
        break;
    }
  }
  if (want == gUi) {
    return;
  }
  if (gUi == UI_READY || gUi == UI_EMPTY) {
    gestureDrop();
  }
  uiSet(want);
}

static void tokenMarkTick() {
  const bool rejected = gConsoleAuthRejected;
  if (rejected == gTokenMarkShown) {
    return;
  }
  gTokenMarkShown = rejected;
  LOG("console token %s\n", rejected ? "rejected" : "ok");
  if (gUi == UI_READY && !gScreenIdle) {
    gNeedFullPaint = true;
  }
}

void loop() {
  const unsigned long now = millis();
  usbPoll();
  bootPoll(now);
  if (gBootPairWant) {
    gBootPairWant = false;
    huePairRequest();
  }
  wifiTick(now);
  hueBindTick();
  if (gWifiWasUp) {
    consolePollTick(now);
  } else {
    consoleApplyConfigIfReady();
  }
  pagesFlushLazy(false);
  applyScreen(now);
  tokenMarkTick();
  if (gUi == UI_READY || gUi == UI_EMPTY) {
    uiPollTouch(now);
  }
  uiTick(now);
}
