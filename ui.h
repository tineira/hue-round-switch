#pragma once

#include <math.h>
#include "channels.h"
#include "display.h"
#include "recipes.h"
#include "touch.h"

// Copy en inglés. El círculo es el producto: gastar tiempo en estos estados.

enum UiScreen {
  UI_BOOT = 0,
  UI_WIFI,
  UI_WIFI_FAIL,
  UI_NO_BRIDGE,
  UI_LOADING,
  UI_PAIRING,
  UI_EMPTY,
  UI_READY,
  UI_BUSY,
  UI_ERROR,
};

static const int16_t kBtnRadius = 88;
static const int16_t kRingGrabInner = 96;
static const int16_t kRingOuter = 118;
static const int16_t kRingInner = 104;
static const float kLevelStartDeg = 135.0f;
static const float kLevelSpanDeg = 270.0f;
static const uint16_t kColBg = 0x10A2;
static const uint16_t kColInk = 0xEF7D;
static const uint16_t kColMute = 0x7BEF;
static const uint16_t kColAmber = 0xFCE0;
static const uint16_t kColAmberDim = 0xA240;
static const uint16_t kColOffFill = 0x2104;
static const uint16_t kColOffInner = 0x3186;
static const uint16_t kColRed = 0xF800;
static const uint16_t kColYellow = 0xFFE0;

inline UiScreen gUi = UI_BOOT;
inline UiScreen gUiPainted = (UiScreen)255;
inline bool gUiPressed = false;
inline unsigned long gUiErrorUntilMs = 0;
inline unsigned long gUiPulseMs = 0;
inline bool gTouchDown = false;
inline unsigned long gTouchDownMs = 0;
inline unsigned long gTouchLastFireMs = 0;
inline bool gDimDragging = false;
inline bool gDimHavePct = false;
inline int gBriPct = 50;
inline bool gBriKnown = false;
inline bool gBriLocal = false;
inline char gBriRid[40] = {0};
inline int gBriShown = -1;
inline int gBriLastSent = -1;
inline bool gLightOn = true;
inline bool gLightOnKnown = false;
inline unsigned long gLightPollMs = 0;
inline unsigned long gTouchLastPtMs = 0;
inline unsigned long gTouchIgnoreUntil = 0;
inline int16_t gTouchX = -1;
inline int16_t gTouchY = -1;

inline const HueRecipe *uiC1Recipe() {
  const HueRecipe *r = recipesFind("c1", "short");
  if (!r) {
    r = recipesFind("c1", "on");
  }
  return r;
}

inline UiScreen uiFromRecipes() { return uiC1Recipe() ? UI_READY : UI_EMPTY; }

inline const HueRecipe *uiDimRecipe() {
  const HueRecipe *r = uiC1Recipe();
  if (!r || !r->rtype[0] || !r->rid[0]) {
    return nullptr;
  }
  if (strcmp(r->rtype, "scene") == 0 || strcmp(r->action, "recall_scene") == 0) {
    return nullptr;
  }
  return r;
}

inline const HueRecipe *uiStateRecipe() {
  const HueRecipe *r = uiC1Recipe();
  if (!r || !r->rtype[0] || !r->rid[0]) {
    return nullptr;
  }
  if (strcmp(r->rtype, "light") == 0 || strcmp(r->rtype, "grouped_light") == 0) {
    return r;
  }
  return nullptr;
}

inline bool uiLightLit() { return !gLightOnKnown || gLightOn; }

inline float uiClockDeg(int16_t x, int16_t y) {
  const float deg = atan2f((float)(x - kScreenCx), (float)(kScreenCy - y)) * (180.0f / 3.14159265f);
  return deg < 0 ? deg + 360.0f : deg;
}

inline int uiClampPct(int v) {
  if (v < 1) {
    return 1;
  }
  if (v > 100) {
    return 100;
  }
  return v;
}

// fillArc: 0° = 3 o'clock, horario. El aro de brillo va de 135° a 135+270°.
inline bool uiTouchToPct(int16_t x, int16_t y, int *pct) {
  if (!pct) {
    return false;
  }
  float fill = uiClockDeg(x, y) - 90.0f;
  if (fill < 0) {
    fill += 360.0f;
  }
  float a = fill - kLevelStartDeg;
  if (a < 0) {
    a += 360.0f;
  }
  if (a > kLevelSpanDeg) {
    return false;
  }
  *pct = uiClampPct((int)(a / kLevelSpanDeg * 99.0f + 1.5f));
  return true;
}

inline void uiFillArcSpan(float start, float end, uint16_t color) {
  if (!gDisplayOk || !gLcd || end <= start + 0.4f) {
    return;
  }
  while (start >= 360.0f && end >= 360.0f) {
    start -= 360.0f;
    end -= 360.0f;
  }
  if (end <= 360.0f) {
    gLcd->fillArc(kScreenCx, kScreenCy, kRingOuter, kRingInner, start, end, color);
    return;
  }
  if (start < 359.5f) {
    gLcd->fillArc(kScreenCx, kScreenCy, kRingOuter, kRingInner, start, 359.6f, color);
  }
  const float wrap = end - 360.0f;
  if (wrap > 0.4f) {
    gLcd->fillArc(kScreenCx, kScreenCy, kRingOuter, kRingInner, 0, wrap, color);
  }
}

inline float uiLevelAngle(int pct) {
  return kLevelStartDeg + (kLevelSpanDeg * (float)uiClampPct(pct)) / 100.0f;
}

inline void uiDrawLevel() {
  if (!gDisplayOk || !gLcd || gUi != UI_READY || !uiDimRecipe()) {
    return;
  }
  const int pct = gBriKnown ? gBriPct : 0;
  const uint16_t fillCol = uiLightLit() ? kColAmber : 0x5AEB;
  if (gBriShown < 0) {
    uiFillArcSpan(kLevelStartDeg, kLevelStartDeg + kLevelSpanDeg, 0x3186);
    if (pct > 0) {
      uiFillArcSpan(kLevelStartDeg, uiLevelAngle(pct), fillCol);
    }
    gBriShown = pct;
    return;
  }
  if (pct == gBriShown) {
    return;
  }
  const bool nearEnd = pct <= 10 || pct >= 90 || gBriShown <= 10 || gBriShown >= 90;
  const bool crossWrap = (gBriShown < 84) != (pct < 84);
  if (nearEnd || crossWrap) {
    gBriShown = -1;
    uiDrawLevel();
    return;
  }
  if (pct > gBriShown) {
    uiFillArcSpan(uiLevelAngle(gBriShown), uiLevelAngle(pct), fillCol);
  } else {
    uiFillArcSpan(uiLevelAngle(pct), uiLevelAngle(gBriShown), 0x3186);
  }
  gBriShown = pct;
}

inline void uiSet(UiScreen s) {
  if (s == UI_ERROR) {
    gUiErrorUntilMs = millis() + 2000;
  }
  if (gUi != s) {
    gUi = s;
  }
}

inline uint16_t uiRingColor(UiScreen s, unsigned long now) {
  switch (s) {
    case UI_LOADING:
      return ((now / 400) % 2) ? kColMute : kColAmberDim;
    case UI_PAIRING:
      return ((now / 280) % 2) ? kColYellow : kColAmberDim;
    case UI_BUSY:
      return ((now / 180) % 2) ? kColInk : kColAmber;
    case UI_ERROR:
      return kColRed;
    case UI_WIFI_FAIL:
    case UI_NO_BRIDGE:
      return kColRed;
    case UI_EMPTY:
      return kColMute;
    case UI_READY:
      if (!uiLightLit()) {
        return gUiPressed ? kColMute : 0x4208;
      }
      return gUiPressed ? kColInk : kColAmber;
    default:
      return kColMute;
  }
}

inline void uiDrawRings(unsigned long now) {
  if (!gDisplayOk || !gLcd) {
    return;
  }
  const uint16_t ring = uiRingColor(gUi, now);
  if (!(gUi == UI_READY && uiDimRecipe())) {
    gLcd->drawCircle(kScreenCx, kScreenCy, kRingOuter, ring);
  }
  gLcd->drawCircle(kScreenCx, kScreenCy, kBtnRadius, ring);
  gLcd->drawCircle(kScreenCx, kScreenCy, kBtnRadius - 1, ring);
  if (gUiPressed && (gUi == UI_READY || gUi == UI_EMPTY)) {
    gLcd->drawCircle(kScreenCx, kScreenCy, kBtnRadius - 2, kColInk);
    gLcd->drawCircle(kScreenCx, kScreenCy, kBtnRadius - 3, kColInk);
  } else {
    uint16_t rest = kColBg;
    if (gUi == UI_READY || gUi == UI_BUSY) {
      rest = uiLightLit() ? kColAmberDim : kColOffInner;
    }
    gLcd->drawCircle(kScreenCx, kScreenCy, kBtnRadius - 2, rest);
    gLcd->drawCircle(kScreenCx, kScreenCy, kBtnRadius - 3, rest);
  }
}

inline const char *uiActionLine(const HueRecipe *r) {
  if (!r) {
    return "Assign in console";
  }
  if (strcmp(r->action, "toggle") == 0) {
    return "Tap to toggle";
  }
  if (strcmp(r->action, "on") == 0) {
    return "Tap to turn on";
  }
  if (strcmp(r->action, "off") == 0) {
    return "Tap to turn off";
  }
  if (strcmp(r->action, "recall_scene") == 0) {
    return "Tap for scene";
  }
  return "Tap";
}

inline void uiDrawButton() {
  if (!gDisplayOk || !gLcd || gUi != UI_READY) {
    return;
  }
  const bool lit = uiLightLit();
  const uint16_t fill = lit ? kColAmberDim : kColOffFill;
  const uint16_t inner = gUiPressed ? (lit ? kColAmber : kColOffInner) : fill;
  gLcd->fillCircle(kScreenCx, kScreenCy, kBtnRadius, fill);
  gLcd->fillCircle(kScreenCx, kScreenCy, kBtnRadius - (gUiPressed ? 4 : 6), inner);
  uiDrawRings(millis());
  displayTextCenter("1", 92, 4, lit ? kColInk : kColMute);
  displayTextCenter(uiActionLine(uiC1Recipe()), 148, 1, lit ? kColInk : kColMute);
  if (uiDimRecipe()) {
    displayTextCenter("Drag ring to dim", 166, 1, kColMute);
  }
}

inline void uiPaint() {
  if (!gDisplayOk || !gLcd) {
    gUiPainted = gUi;
    return;
  }

  const unsigned long now = millis();
  gLcd->fillScreen(kColBg);

  if (gUi == UI_READY) {
    uiDrawButton();
  } else {
    const uint16_t ring = uiRingColor(gUi, now);
    gLcd->fillCircle(kScreenCx, kScreenCy, kBtnRadius, kColBg);
    gLcd->drawCircle(kScreenCx, kScreenCy, kBtnRadius, ring);
    gLcd->drawCircle(kScreenCx, kScreenCy, kBtnRadius - 1, ring);
    gLcd->drawCircle(kScreenCx, kScreenCy, kRingOuter, ring);
    displayTextCenter("1", 92, 4, kColMute);
  }

  const char *line = "";
  switch (gUi) {
    case UI_BOOT:
    case UI_WIFI:
      line = "Wi-Fi...";
      break;
    case UI_WIFI_FAIL:
      line = "No Wi-Fi";
      break;
    case UI_NO_BRIDGE:
      line = "No Bridge";
      break;
    case UI_LOADING:
      line = "Loading...";
      break;
    case UI_PAIRING:
      line = "Press Bridge button";
      break;
    case UI_EMPTY:
      line = "Assign in console";
      break;
    case UI_READY:
      break;
    case UI_BUSY:
      line = "...";
      break;
    case UI_ERROR:
      line = "Hue error";
      break;
  }
  if (gUi != UI_READY) {
    displayTextCenter(line, 148, 1, kColInk);
  }

  if (gUi == UI_WIFI_FAIL) {
    displayTextCenter("Plug the antenna", 168, 1, kColMute);
  } else if (gUi == UI_LOADING) {
    displayTextCenter("Connecting", 166, 1, kColMute);
  } else if (gUi == UI_PAIRING) {
    displayTextCenter("on the Hue Bridge", 166, 1, kColMute);
  } else if (gUi == UI_EMPTY) {
    displayTextCenter("Channel 1", 166, 1, kColMute);
  } else if (gUi == UI_NO_BRIDGE) {
    displayTextCenter("same LAN as Bridge", 166, 1, kColMute);
  }

  if (gUi == UI_READY) {
    gBriShown = -1;
    uiDrawLevel();
  }

  gUiPainted = gUi;
  gUiPulseMs = now;
}

inline bool uiNeedsPulse(UiScreen s) { return s == UI_PAIRING || s == UI_LOADING; }

inline bool uiRefreshState(bool force) {
  const HueRecipe *r = uiStateRecipe();
  if (!r) {
    gLightOnKnown = false;
    gBriKnown = false;
    gBriRid[0] = 0;
    return false;
  }
  if (!force && gBriLocal && gLightOnKnown && gBriRid[0] && strcmp(gBriRid, r->rid) == 0) {
    return false;
  }
  bool on = gLightOn;
  int pct = gBriPct;
  if (!hueGetLightState(r->rtype, r->rid, &on, uiDimRecipe() ? &pct : nullptr)) {
    return false;
  }
  recipeCopyField(gBriRid, sizeof(gBriRid), r->rid);
  const bool onChanged = !gLightOnKnown || on != gLightOn;
  const bool briChanged = uiDimRecipe() && (!gBriKnown || uiClampPct(pct) != gBriPct);
  gLightOn = on;
  gLightOnKnown = true;
  if (uiDimRecipe()) {
    gBriPct = uiClampPct(pct);
    gBriKnown = true;
  }
  gBriLocal = false;
  return onChanged || briChanged;
}

inline void uiLoadLevel() { uiRefreshState(false); }

inline void uiSyncFace() {
  if (gUi != UI_READY) {
    return;
  }
  uiDrawButton();
  if (uiDimRecipe()) {
    gBriShown = -1;
    uiDrawLevel();
  }
}

inline void uiSetLightOn(bool on) {
  const bool changed = !gLightOnKnown || on != gLightOn;
  gLightOn = on;
  gLightOnKnown = true;
  if (changed) {
    uiSyncFace();
  }
}

inline void uiTick(unsigned long now) {
  if (gDimDragging) {
    return;
  }
  if (gUi == UI_ERROR && now >= gUiErrorUntilMs) {
    uiSet(uiFromRecipes());
  }
  if (gUi != gUiPainted) {
    if (gUi == UI_READY) {
      uiLoadLevel();
    }
    uiPaint();
    return;
  }
  if (uiNeedsPulse(gUi) && (now - gUiPulseMs) >= 180) {
    uiDrawRings(now);
    gUiPulseMs = now;
  }
  if (gUi == UI_READY && !gTouchDown && now - gLightPollMs >= 20000) {
    gLightPollMs = now;
    if (uiRefreshState(true)) {
      uiSyncFace();
    }
  }
}

inline bool uiDimPut(int pct) {
  const HueRecipe *r = uiDimRecipe();
  if (!r || WiFi.status() != WL_CONNECTED) {
    return false;
  }
  pct = uiClampPct(pct);
  if (pct == gBriLastSent) {
    return true;
  }
  if (!hueSetBrightness(r->rtype, r->rid, pct)) {
    return false;
  }
  gBriLastSent = pct;
  gBriPct = pct;
  gBriKnown = true;
  gBriLocal = true;
  uiSetLightOn(true);
  return true;
}

inline void uiDimFromPoint(int16_t x, int16_t y) {
  int pct = 0;
  if (!uiTouchToPct(x, y, &pct)) {
    return;
  }
  if (gDimHavePct && abs(pct - gBriPct) > 18) {
    return;
  }
  gDimHavePct = true;
  gBriPct = pct;
  gBriKnown = true;
  gBriLocal = true;
  if (!uiLightLit()) {
    gLightOn = true;
    gLightOnKnown = true;
    uiDrawButton();
    gBriShown = -1;
  }
  if (pct != gBriShown) {
    uiDrawLevel();
  }
}

inline bool uiOnTap() {
  if (gUi == UI_WIFI || gUi == UI_WIFI_FAIL || gUi == UI_NO_BRIDGE || gUi == UI_LOADING || gUi == UI_PAIRING ||
      gUi == UI_BOOT) {
    return false;
  }
  if (!uiC1Recipe()) {
    uiSet(UI_EMPTY);
    uiPaint();
    return false;
  }
  bool on = gLightOn;
  const bool ok = recipeFire("c1", "short", &on);
  if (!ok) {
    gUiPressed = false;
    uiSet(UI_ERROR);
    uiPaint();
    return false;
  }
  uiSetLightOn(on);
  return true;
}

inline void uiTouchEnd(unsigned long now) {
  if (gDimDragging) {
    if (gBriKnown) {
      uiDimPut(gBriPct);
    }
    Serial.printf("dim %d%%\n", gBriPct);
    gDimDragging = false;
    gDimHavePct = false;
    if (gUi == UI_READY) {
      uiDrawButton();
      gBriShown = -1;
      uiDrawLevel();
    }
  }
  const bool wasPressed = gUiPressed;
  gTouchDown = false;
  gUiPressed = false;
  gTouchIgnoreUntil = now + 40;
  if (gUi == UI_READY) {
    uiDrawButton();
    if (uiDimRecipe()) {
      uiDrawLevel();
    }
  } else if (wasPressed && gUi == UI_EMPTY) {
    uiDrawRings(now);
  }
}

inline void uiPollTouch(unsigned long now) {
  const bool irq = touchIrqPressed();
  int16_t x = 0, y = 0;
  bool hasPt = false;
  if (irq || gTouchDown) {
    hasPt = touchReadXY(&x, &y);
  }
  const bool moved = hasPt && (abs(x - gTouchX) + abs(y - gTouchY) >= 2);

  if (gTouchDown) {
    if (!hasPt && (now - gTouchLastPtMs) >= 80) {
      uiTouchEnd(now);
      return;
    }
    if (hasPt && !irq && !moved && (now - gTouchLastPtMs) >= 400) {
      uiTouchEnd(now);
      return;
    }
  }

  if (!gTouchDown) {
    if (!irq || !hasPt || now < gTouchIgnoreUntil) {
      return;
    }
    gTouchDown = true;
    gTouchDownMs = now;
    gTouchLastPtMs = now;
    gTouchX = x;
    gTouchY = y;
    const bool inBtn = touchHitButton(x, y, kBtnRadius);
    const bool inRing = touchHitRing(x, y, kRingGrabInner, kRingOuter + 8);
    if (gUi == UI_READY && inRing && uiDimRecipe()) {
      gDimDragging = true;
      gDimHavePct = false;
      Serial.printf("ring raw %u,%u xy %d,%d full=%d\n", gTouchRawX, gTouchRawY, x, y,
                    (int)gTouchFullRange);
      uiDimFromPoint(x, y);
      return;
    }
    if (inBtn && (gUi == UI_READY || gUi == UI_EMPTY)) {
      gUiPressed = true;
      uiDrawRings(now);
      if (now - gTouchLastFireMs >= 250) {
        gTouchLastFireMs = now;
        uiOnTap();
      }
    }
    return;
  }

  if (moved) {
    gTouchLastPtMs = now;
    gTouchX = x;
    gTouchY = y;
    if (gDimDragging) {
      uiDimFromPoint(x, y);
    }
  } else if (irq) {
    gTouchLastPtMs = now;
  }
}
