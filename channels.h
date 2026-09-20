#pragma once

#include <WiFi.h>
#include "hue.h"
#include "hue_discover.h"
#include "recipes.h"

// v1: un canal de pantalla, no un contacto de pared.
// gpio 0 es placeholder: la consola exige gpio >= 0; no es un INPUT_PULLUP.

enum ChannelKind { CH_MAINTAINED, CH_MOMENTARY };

struct ChannelDef {
  const char *id;
  int gpio;
  const char *label;
  ChannelKind kind;
};

static const ChannelDef kChannels[] = {
    {"c1", 0, "1", CH_MOMENTARY},
};

static const size_t kChannelCount = sizeof(kChannels) / sizeof(kChannels[0]);
static const int kBootPin = 0;  // botón BOOT del XIAO, re-pair Hue
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

inline void channelsAppendJson(String &out) {
  out += '[';
  for (size_t i = 0; i < kChannelCount; i++) {
    if (i) {
      out += ',';
    }
    out += "{\"id\":";
    jsonAppendEscaped(out, kChannels[i].id);
    out += ",\"gpio\":";
    out += String(kChannels[i].gpio);
    out += ",\"label\":";
    jsonAppendEscaped(out, kChannels[i].label);
    out += ",\"kind\":";
    jsonAppendEscaped(out, kChannels[i].kind == CH_MOMENTARY ? "momentary" : "maintained");
    out += '}';
  }
  out += ']';
}

inline bool recipeFire(const char *channelId, const char *event, bool *nowOn = nullptr) {
  const HueRecipe *r = recipesFind(channelId, event);
  if (!r && strcmp(event, "double_click") == 0) {
    Serial.printf("%s double_click: no recipe, fallback on\n", channelId);
    r = recipesFind(channelId, "on");
  }
  if (!r) {
    Serial.printf("%s %s: no recipe\n", channelId, event);
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("%s %s skipped: WiFi down\n", channelId, event);
    return false;
  }
  Serial.printf("%s %s -> %s %s/%s\n", channelId, event, r->action, r->rtype, r->rid);
  if (!hueExecute(r->action, r->rtype, r->rid, nowOn)) {
    Serial.println("Hue action failed");
    return false;
  }
  return true;
}

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
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Re-pair skipped: WiFi down");
    } else if (hueRePair()) {
      Serial.printf("Using Bridge %s\n", gHueBridgeIp.c_str());
      recipesBindBridge(gHueBridgeId);
      gNeedConsoleSync = true;
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
