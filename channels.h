#pragma once

#include <WiFi.h>
#include "hue.h"
#include "hue_discover.h"
#include "pages.h"
#include "recipes.h"

// Round declares no GPIO. The register's channels[] is empty.
// Physical BOOT (GPIO 0, 3 s) is still Hue re-pair, not a recipe.

static const int kBootPin = 0;
static const unsigned long kDebounceMs = 50;

struct BootRuntime {
  int lastReading;
  int stable;
  unsigned long lastChangeMs;
  unsigned long pressStartMs;
  bool longPressHandled;
  bool primed;
};

inline BootRuntime gBoot;

inline void channelsAppendJson(String &out) { out += "[]"; }

inline void bootBegin() {
  pinMode(kBootPin, INPUT_PULLUP);
  const int v = digitalRead(kBootPin);
  gBoot.lastReading = v;
  gBoot.stable = v;
  gBoot.lastChangeMs = millis();
  gBoot.pressStartMs = 0;
  gBoot.longPressHandled = false;
  gBoot.primed = true;
}

inline void bootPoll(unsigned long now) {
  if (!gBoot.primed) {
    return;
  }
  const int reading = digitalRead(kBootPin);
  if (reading != gBoot.lastReading) {
    gBoot.lastChangeMs = now;
    gBoot.lastReading = reading;
  }
  if ((now - gBoot.lastChangeMs) < kDebounceMs) {
    return;
  }

  if (reading == LOW && gBoot.stable == LOW && gBoot.pressStartMs && !gBoot.longPressHandled &&
      (now - gBoot.pressStartMs) >= kLongPressMs) {
    gBoot.longPressHandled = true;
    if (gWifiStaForgotten || WiFi.status() != WL_CONNECTED) {
      LOGLN("Re-pair skipped: WiFi down");
    } else if (gHuePairBusy || gHuePairReq) {
      LOGLN("Re-pair skipped: already pairing");
    } else {
      gHuePairCancel = false;
      if (hueRePair()) {
        hueStrLock();
        const String ip = gHueBridgeIp;
        const String id = gHueBridgeId;
        hueStrUnlock();
        LOG("Using Bridge %s\n", ip.c_str());
        recipesBindBridge(id);
        pagesBindBridge(id);
        gNeedConsoleSync = true;
      }
    }
    return;
  }

  if (reading == gBoot.stable) {
    return;
  }
  gBoot.stable = reading;
  if (reading == LOW) {
    gBoot.pressStartMs = now;
    gBoot.longPressHandled = false;
    return;
  }
  gBoot.pressStartMs = 0;
}
