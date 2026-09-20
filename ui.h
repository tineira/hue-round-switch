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
  if (gBriShown < 0) {
    uiFillArcSpan(kLevelStartDeg, kLevelStartDeg + kLevelSpanDeg, 0x3186);
    if (pct > 0) {
      uiFillArcSpan(kLevelStartDeg, uiLevelAngle(pct), kColAmber);
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
    uiFillArcSpan(uiLevelAngle(gBriShown), uiLevelAngle(pct), kColAmber);
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
    const uint16_t rest = (gUi == UI_READY || gUi == UI_BUSY) ? kColAmberDim : kColBg;
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

inline void uiPaint() {
  if (!gDisplayOk || !gLcd) {
    gUiPainted = gUi;
    return;
  }

  const unsigned long now = millis();
  gLcd->fillScreen(kColBg);

  const uint16_t ring = uiRingColor(gUi, now);
  gLcd->fillCircle(kScreenCx, kScreenCy, kBtnRadius, gUi == UI_READY || gUi == UI_BUSY ? kColAmberDim : kColBg);
  if (gUi == UI_READY && gUiPressed) {
    gLcd->fillCircle(kScreenCx, kScreenCy, kBtnRadius - 4, kColAmber);
  } else if (gUi == UI_READY || gUi == UI_BUSY) {
    gLcd->fillCircle(kScreenCx, kScreenCy, kBtnRadius - 6, kColAmberDim);
  }
  gLcd->drawCircle(kScreenCx, kScreenCy, kBtnRadius, ring);
  gLcd->drawCircle(kScreenCx, kScreenCy, kBtnRadius - 1, ring);
  if (!(gUi == UI_READY && uiDimRecipe())) {
    gLcd->drawCircle(kScreenCx, kScreenCy, kRingOuter, ring);
  }

  displayTextCenter("1", 92, 4,
                    gUi == UI_EMPTY || gUi == UI_WIFI || gUi == UI_BOOT || gUi == UI_LOADING ? kColMute : kColInk);

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
      line = uiActionLine(uiC1Recipe());
      break;
    case UI_BUSY:
      line = "...";
      break;
    case UI_ERROR:
      line = "Hue error";
      break;
  }
  displayTextCenter(line, 148, 1, kColInk);

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
  } else if (gUi == UI_READY && uiDimRecipe()) {
    displayTextCenter("Drag ring to dim", 166, 1, kColMute);
  }

  if (gUi == UI_READY) {
    gBriShown = -1;
    uiDrawLevel();
  }

  gUiPainted = gUi;
  gUiPulseMs = now;
}

inline bool uiNeedsPulse(UiScreen s) { return s == UI_PAIRING || s == UI_LOADING; }

inline void uiLoadLevel() {
  const HueRecipe *r = uiDimRecipe();
  if (!r) {
    gBriKnown = false;
    gBriLocal = false;
    gBriRid[0] = 0;
    return;
  }
  if (gBriLocal) {
    return;
  }
  if (gBriKnown && gBriRid[0] && strcmp(gBriRid, r->rid) == 0) {
    return;
  }
  int pct = 50;
  if (!hueGetBrightness(r->rtype, r->rid, &pct)) {
    return;
  }
  recipeCopyField(gBriRid, sizeof(gBriRid), r->rid);
  gBriPct = uiClampPct(pct);
  gBriKnown = true;
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
  const bool ok = recipeFire("c1", "short");
  if (!ok) {
    gUiPressed = false;
    uiSet(UI_ERROR);
    uiPaint();
    return false;
  }
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
  }
  const bool wasPressed = gUiPressed;
  gTouchDown = false;
  gUiPressed = false;
  gTouchIgnoreUntil = now + 40;
  if (wasPressed && (gUi == UI_READY || gUi == UI_EMPTY)) {
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
