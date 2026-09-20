#include <WiFi.h>
#include "config.h"

#define FIRMWARE_VERSION "0.2.0"

String gHueBridgeIp;
String gHueAppKey;

#include "hue.h"
#include "hue_discover.h"
#include "recipes.h"
#include "display.h"
#include "touch.h"
#include "channels.h"
#include "ui.h"
#include "console.h"

static bool gWifiWasUp = false;
static bool gHueReady = false;
static unsigned long gWifiLastTryMs = 0;

static bool wifiWait(unsigned long maxMs) {
  const unsigned long start = millis();
  Serial.print("WiFi");
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < maxMs) {
    delay(250);
    Serial.print(".");
    uiTick(millis());
  }
  Serial.println();
  return WiFi.status() == WL_CONNECTED;
}

static void afterWifiUp() {
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
  Serial.printf("mac %s\n", deviceMacHex().c_str());
  digitalWrite(LED_BUILTIN, HIGH);

  uiSet(UI_LOADING);
  uiPaint();
  gOnHueWait = []() { uiTick(millis()); };
  gOnHuePairing = [](bool pairing) {
    uiSet(pairing ? UI_PAIRING : UI_LOADING);
    uiPaint();
  };

  if (!hueEnsureReady()) {
    Serial.println("Hue setup failed - press Bridge button if pairing, check Wi-Fi LAN");
    uiSet(UI_NO_BRIDGE);
    uiPaint();
    gHueReady = false;
    return;
  }
  gHueReady = true;
  Serial.printf("Using Bridge %s id=%s\n", gHueBridgeIp.c_str(), gHueBridgeId.c_str());
  recipesBindBridge(gHueBridgeId);
  consoleBootSync();
  uiSet(uiFromRecipes());
  if (gUi == UI_READY) {
    uiLoadLevel();
  }
  uiPaint();
  Serial.println("tap c1 = recipe. ring = dim. BOOT hold 3s = re-pair.");
}

void setup() {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);
  delay(200);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  Serial.println("hue-round-switch");
  Serial.printf("firmware %s  SSID: %s\n", FIRMWARE_VERSION, WIFI_SSID);

  recipesLoad();
  bootBegin();
  displayBegin();
  touchBegin();

  uiSet(UI_WIFI);
  uiPaint();

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  gWifiLastTryMs = millis();

  if (!wifiWait(15000)) {
    Serial.printf("WiFi failed, status=%d (S3 needs the U.FL antenna)\n", (int)WiFi.status());
    uiSet(UI_WIFI_FAIL);
    uiPaint();
    return;
  }

  gWifiWasUp = true;
  afterWifiUp();
}

void loop() {
  const unsigned long now = millis();

  if (WiFi.status() != WL_CONNECTED) {
    gWifiWasUp = false;
    if (gUi != UI_WIFI && gUi != UI_WIFI_FAIL) {
      uiSet(UI_WIFI_FAIL);
    }
    if (now - gWifiLastTryMs >= 10000) {
      gWifiLastTryMs = now;
      uiSet(UI_WIFI);
      uiPaint();
      Serial.println("WiFi retry");
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      wifiWait(8000);
    }
    bootPoll(now);
    uiTick(now);
    return;
  }

  if (!gWifiWasUp) {
    gWifiWasUp = true;
    afterWifiUp();
  }

  bootPoll(now);
  if (gHueReady) {
    uiPollTouch(now);
    if (!gDimDragging) {
      consolePollTick(now);
    }
    if (gUi == UI_EMPTY || gUi == UI_READY || gUi == UI_ERROR) {
      const UiScreen next = uiFromRecipes();
      if (gUi != UI_ERROR && next != gUi) {
        uiSet(next);
      }
    }
  }
  uiTick(now);
}
