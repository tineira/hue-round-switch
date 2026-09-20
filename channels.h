#pragma once

#include <WiFi.h>
#include "hue.h"
#include "hue_discover.h"
#include "pages.h"
#include "recipes.h"

// Round no declara GPIO. channels[] del registro va vacío.
// BOOT físico (GPIO 0, 3 s) sigue siendo re-pair Hue, no receta.

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

inline int recipeFindActiveScene(const HueRecipe *r) {
  if (!r || r->sceneCount == 0) {
    return -1;
  }
  const char *last = pagesLastSceneRid();
  if (last && last[0]) {
    for (uint8_t i = 0; i < r->sceneCount; i++) {
      if (strcmp(r->scenes[i].rid, last) != 0) {
        continue;
      }
      bool act = false;
      if (hueSceneActive(r->scenes[i].rid, &act) && act) {
        return static_cast<int>(i);
      }
      break;
    }
  }
  for (uint8_t i = 0; i < r->sceneCount; i++) {
    if (!r->scenes[i].rid[0]) {
      continue;
    }
    if (last && last[0] && strcmp(r->scenes[i].rid, last) == 0) {
      continue;
    }
    bool act = false;
    if (hueSceneActive(r->scenes[i].rid, &act) && act) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

inline bool recipeRecallNext(const HueRecipe *r, bool *nowOn) {
  if (!r || r->sceneCount == 0) {
    return false;
  }
  const int cur = recipeFindActiveScene(r);
  uint8_t next = (cur >= 0) ? static_cast<uint8_t>((cur + 1) % r->sceneCount) : 0;
  for (uint8_t n = 0; n < r->sceneCount; n++) {
    const uint8_t i = static_cast<uint8_t>((next + n) % r->sceneCount);
    if (!r->scenes[i].rid[0]) {
      continue;
    }
    if (!hueRecallScene(r->scenes[i].rid)) {
      return false;
    }
    pagesSetLastSceneRid(r->scenes[i].rid);
    if (nowOn) {
      *nowOn = true;
    }
    return true;
  }
  return false;
}

enum FireResult { FIRE_NONE, FIRE_OK, FIRE_ERR };

inline FireResult recipeFire(const char *pageId, const char *event, bool *nowOn = nullptr) {
  const HueRecipe *r = recipesFind(pageId, event);
  if (!r) {
    Serial.printf("%s %s: no recipe\n", pageId ? pageId : "?", event ? event : "?");
    return FIRE_NONE;
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("%s %s skipped: WiFi down\n", pageId, event);
    return FIRE_ERR;
  }
  if (strcmp(r->action, "recall_scene") == 0) {
    Serial.printf("%s %s -> cycle %u scenes\n", pageId, event, r->sceneCount);
    if (!recipeRecallNext(r, nowOn)) {
      Serial.println("Hue scene cycle failed");
      return FIRE_ERR;
    }
    return FIRE_OK;
  }
  Serial.printf("%s %s -> %s %s/%s\n", pageId, event, r->action, r->rtype, r->rid);
  if (!hueExecute(r->action, r->rtype, r->rid, nowOn)) {
    Serial.println("Hue action failed");
    return FIRE_ERR;
  }
  if (strcmp(r->action, "off") == 0) {
    pagesSetLastSceneRid("");
  }
  return FIRE_OK;
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
      pagesBindBridge(gHueBridgeId);
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
