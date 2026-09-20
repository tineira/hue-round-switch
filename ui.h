#pragma once

#include <math.h>
#include "channels.h"
#include "display.h"
#include "hue_job.h"
#include "pages.h"
#include "recipes.h"
#include "touch.h"

// Copy en inglés. Ready no enseña gestos.

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

enum TouchMode { TOUCH_IDLE = 0, TOUCH_CENTER, TOUCH_RING };

static const int16_t kBtnRadius = 88;
static const int16_t kRingGrabInner = 96;
static const int16_t kRingOuter = 118;
static const int16_t kRingInner = 104;
static const float kLevelStartDeg = 135.0f;
static const float kLevelSpanDeg = 270.0f;
static const int16_t kNameY = 92;
static const int16_t kSceneY = 114;
static const int16_t kDotsY = 197;
static const unsigned long kDoubleTapMs = 350;
static const int16_t kSwipeMinPx = 40;
static const uint16_t kColYellow = 0xFFE0;

inline UiScreen gUi = UI_BOOT;
inline UiScreen gUiPainted = (UiScreen)255;
inline bool gUiPressed = false;
inline unsigned long gUiErrorUntilMs = 0;
inline unsigned long gUiPulseMs = 0;
inline bool gTouchDown = false;
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
inline bool gTapOn = false;
inline bool gTapOnKnown = false;
inline bool gDblOn = false;
inline bool gDblOnKnown = false;
inline unsigned long gLightPollMs = 0;
inline unsigned long gTouchLastPtMs = 0;
inline unsigned long gTouchIgnoreUntil = 0;
inline int16_t gTouchX = -1;
inline int16_t gTouchY = -1;
inline int16_t gTouchStartX = -1;
inline int16_t gTouchStartY = -1;
inline TouchMode gTouchMode = TOUCH_IDLE;
inline bool gSwipeDone = false;
inline bool gTapWaitDouble = false;
inline bool gSecondTap = false;
inline unsigned long gTapWaitMs = 0;
inline bool gSceneHave = false;
inline char gSceneName[25] = {0};

inline UiScreen uiFromRecipes() { return gPageCount > 0 ? UI_READY : UI_EMPTY; }

inline bool uiPageHasDouble() { return recipesFind(pagesActiveId(), "double_click") != nullptr; }

inline bool uiHasDim() { return pagesHasDim(); }

inline bool uiSplitTwoLights() { return recipesTwoChildLights(pagesActiveId(), nullptr, nullptr); }

inline bool uiLightLit() {
  if (uiSplitTwoLights()) {
    return (gTapOnKnown && gTapOn) || (gDblOnKnown && gDblOn);
  }
  return !gLightOnKnown || gLightOn;
}

inline const PageTheme *uiTheme() {
  if (gUi == UI_READY || gUi == UI_BUSY || gUi == UI_EMPTY) {
    return pagesActiveTheme();
  }
  return themeEmber();
}

inline uint16_t uiFillCol(const PageTheme *t) { return uiLightLit() ? t->fillOn : t->fillOff; }

inline uint16_t uiInkCol(const PageTheme *t) { return uiLightLit() ? t->ink : t->mute; }

inline uint16_t uiSceneCol(const PageTheme *t) {
  return colorMix565(t->ink, uiFillCol(t), 88);
}

inline uint16_t uiDotIdleCol(const PageTheme *t) {
  return colorMix565(t->ink, uiFillCol(t), 42);
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

inline int16_t uiDiscChord(int16_t y, int16_t r) {
  const int32_t dy = (int32_t)y - kScreenCy;
  const int32_t r2 = (int32_t)r * r;
  const int32_t d2 = dy * dy;
  if (d2 >= r2) {
    return 0;
  }
  return (int16_t)(2.0f * sqrtf((float)(r2 - d2)));
}

inline void uiFillDiscSide(bool right, int16_t radius, uint16_t color) {
  if (!gDisplayOk || !gLcd || radius <= 0) {
    return;
  }
  for (int16_t y = kScreenCy - radius; y <= kScreenCy + radius; y++) {
    const int16_t w = uiDiscChord(y, radius);
    if (w <= 1) {
      continue;
    }
    const int16_t xL = kScreenCx - w / 2;
    const int16_t xR = xL + w - 1;
    if (right) {
      if (xR >= kScreenCx) {
        gLcd->drawFastHLine(kScreenCx, y, xR - kScreenCx + 1, color);
      }
    } else if (xL < kScreenCx) {
      const int16_t xEnd = xR < kScreenCx ? xR : (int16_t)(kScreenCx - 1);
      gLcd->drawFastHLine(xL, y, xEnd - xL + 1, color);
    }
  }
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
  if (!gDisplayOk || !gLcd || gUi != UI_READY || !uiHasDim()) {
    return;
  }
  const PageTheme *t = uiTheme();
  const int pct = gBriKnown ? gBriPct : 0;
  const uint16_t fillCol = uiLightLit() ? t->accent : colorMix565(t->accent, t->ringTrack, 40);
  const uint16_t track = t->ringTrack;
  if (gBriShown < 0) {
    uiFillArcSpan(kLevelStartDeg, kLevelStartDeg + kLevelSpanDeg, track);
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
    uiFillArcSpan(uiLevelAngle(pct), uiLevelAngle(gBriShown), track);
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
  const PageTheme *t = uiTheme();
  switch (s) {
    case UI_LOADING:
      return ((now / 400) % 2) ? t->mute : colorMix565(t->accent, t->bg, 50);
    case UI_PAIRING:
      return ((now / 280) % 2) ? kColYellow : t->accent;
    case UI_BUSY:
      return ((now / 180) % 2) ? t->ink : t->accent;
    case UI_ERROR:
    case UI_WIFI_FAIL:
    case UI_NO_BRIDGE:
      return t->error;
    case UI_EMPTY:
      return t->mute;
    case UI_READY:
      if (!uiLightLit()) {
        return gUiPressed ? t->mute : t->ringTrack;
      }
      return gUiPressed ? t->ink : t->accent;
    default:
      return t->mute;
  }
}

inline void uiDrawRings(unsigned long now) {
  if (!gDisplayOk || !gLcd) {
    return;
  }
  const PageTheme *t = uiTheme();
  const uint16_t ring = uiRingColor(gUi, now);
  if (!(gUi == UI_READY && uiHasDim())) {
    gLcd->drawCircle(kScreenCx, kScreenCy, kRingOuter, ring);
  }
  gLcd->drawCircle(kScreenCx, kScreenCy, kBtnRadius, ring);
  gLcd->drawCircle(kScreenCx, kScreenCy, kBtnRadius - 1, ring);
  if (gUiPressed && (gUi == UI_READY || gUi == UI_EMPTY)) {
    gLcd->drawCircle(kScreenCx, kScreenCy, kBtnRadius - 2, t->ink);
    gLcd->drawCircle(kScreenCx, kScreenCy, kBtnRadius - 3, t->ink);
  } else {
    uint16_t rest = t->bg;
    if (gUi == UI_READY || gUi == UI_BUSY) {
      rest = uiLightLit() ? colorMix565(t->fillOn, t->accent, 70) : colorMix565(t->fillOff, t->ink, 85);
    }
    gLcd->drawCircle(kScreenCx, kScreenCy, kBtnRadius - 2, rest);
    gLcd->drawCircle(kScreenCx, kScreenCy, kBtnRadius - 3, rest);
  }
}

inline void uiDrawPageDots(const PageTheme *t) {
  if (!gDisplayOk || !gLcd || gPageCount < 2) {
    return;
  }
  const uint8_t n = gPageCount;
  const int16_t y = kDotsY;
  const int16_t budget = uiDiscChord(y, kBtnRadius) - 16;
  int d = 6;
  int gap = 10;
  auto width = [&]() { return n * d + (n - 1) * gap; };
  if (width() > budget) {
    gap = 4;
  }
  if (width() > budget) {
    d = 4;
    gap = 4;
  }
  if (width() > budget || budget < 8) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%u/%u", gPageIndex + 1, n);
    displayTextCenter(buf, y, 1, uiDotIdleCol(t));
    return;
  }
  const int total = width();
  const int16_t x0 = kScreenCx - (int16_t)total / 2 + d / 2;
  const uint16_t idle = uiDotIdleCol(t);
  const uint16_t on = t->ink;
  for (uint8_t i = 0; i < n; i++) {
    const int16_t x = x0 + (int16_t)i * (int16_t)(d + gap);
    gLcd->fillCircle(x, y, d / 2, i == gPageIndex ? on : idle);
  }
}

inline void uiDrawReadyFace() {
  if (!gDisplayOk || !gLcd) {
    return;
  }
  const PageTheme *t = uiTheme();
  const bool split = uiSplitTwoLights();
  const bool tapLit = !gTapOnKnown || gTapOn;
  const bool dblLit = !gDblOnKnown || gDblOn;
  const bool lit = uiLightLit();
  const int16_t innerR = kBtnRadius - (gUiPressed ? 4 : 6);
  if (split) {
    const uint16_t left = gTapOnKnown && gTapOn ? t->fillOn : t->fillOff;
    const uint16_t right = gDblOnKnown && gDblOn ? t->fillOn : t->fillOff;
    uiFillDiscSide(false, kBtnRadius, left);
    uiFillDiscSide(true, kBtnRadius, right);
    const uint16_t leftIn =
        gUiPressed ? (tapLit ? t->accent : colorMix565(left, t->ink, 80)) : left;
    const uint16_t rightIn =
        gUiPressed ? (dblLit ? t->accent : colorMix565(right, t->ink, 80)) : right;
    uiFillDiscSide(false, innerR, leftIn);
    uiFillDiscSide(true, innerR, rightIn);
  } else {
    const uint16_t fill = uiFillCol(t);
    const uint16_t inner = gUiPressed ? (lit ? t->accent : colorMix565(fill, t->ink, 80)) : fill;
    gLcd->fillCircle(kScreenCx, kScreenCy, kBtnRadius, fill);
    gLcd->fillCircle(kScreenCx, kScreenCy, innerR, inner);
  }
  uiDrawRings(millis());

  const Page *p = pagesActive();
  const char *name = p && p->name[0] ? p->name : "Page";
  const int16_t nameBudget = uiDiscChord(kNameY, kBtnRadius) - 16;
  displayTextEllipsis(name, kNameY, 2, uiInkCol(t), nameBudget, 1);

  if (!split && gSceneHave && gSceneName[0] && lit) {
    const int16_t sceneBudget = uiDiscChord(kSceneY, kBtnRadius) - 16;
    displayTextEllipsis(gSceneName, kSceneY, 1, uiSceneCol(t), sceneBudget, 8);
  }
  uiDrawPageDots(t);
}

inline void uiPaint() {
  if (!gDisplayOk || !gLcd) {
    gUiPainted = gUi;
    return;
  }

  const unsigned long now = millis();
  const PageTheme *t = uiTheme();
  gLcd->fillScreen(t->bg);

  if (gUi == UI_READY || gUi == UI_EMPTY) {
    uiDrawReadyFace();
  } else {
    const uint16_t ring = uiRingColor(gUi, now);
    gLcd->fillCircle(kScreenCx, kScreenCy, kBtnRadius, t->bg);
    gLcd->drawCircle(kScreenCx, kScreenCy, kBtnRadius, ring);
    gLcd->drawCircle(kScreenCx, kScreenCy, kBtnRadius - 1, ring);
    gLcd->drawCircle(kScreenCx, kScreenCy, kRingOuter, ring);
  }

  const char *line = "";
  switch (gUi) {
    case UI_BOOT:
      line = "";
      break;
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
    case UI_READY:
      break;
    case UI_BUSY:
      line = "...";
      break;
    case UI_ERROR:
      line = "Hue error";
      break;
  }
  if (gUi == UI_BOOT) {
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "?"
#endif
    displayTextCenter("Round", 92, 1, t->mute);
    displayTextCenter(FIRMWARE_VERSION, 120, 2, t->ink);
  } else if (gUi == UI_WIFI) {
    displayTextCenter(line, 148, 1, t->ink);
    displayTextCenter(FIRMWARE_VERSION, 176, 1, t->mute);
  } else if (gUi != UI_READY && gUi != UI_EMPTY) {
    displayTextCenter(line, 148, 1, t->ink);
  }

  if (gUi == UI_WIFI_FAIL) {
    displayTextCenter("Plug the antenna", 168, 1, t->mute);
  } else if (gUi == UI_LOADING) {
    displayTextCenter("Connecting", 166, 1, t->mute);
  } else if (gUi == UI_PAIRING) {
    displayTextCenter("on the Hue Bridge", 166, 1, t->mute);
  } else if (gUi == UI_NO_BRIDGE) {
    displayTextCenter("same LAN as Bridge", 166, 1, t->mute);
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
  const Page *p = pagesActive();
  const char *rtype = nullptr;
  const char *rid = nullptr;
  const bool dimRing = uiHasDim();
  const bool lightsDim = p && p->dimMode == PAGE_DIM_LIGHTS && p->dimLightCount > 0;

  if (p && p->dimMode == PAGE_DIM_GROUP && p->dimGroupRid[0]) {
    rtype = "grouped_light";
    rid = p->dimGroupRid;
  } else if (lightsDim) {
    rid = p->dimLights[0];
  } else {
    const HueRecipe *r = recipesFind(pagesActiveId(), "short");
    if (r && r->rid[0] && (strcmp(r->rtype, "light") == 0 || strcmp(r->rtype, "grouped_light") == 0)) {
      rtype = r->rtype;
      rid = r->rid;
    }
  }
  if (!rid || !rid[0]) {
    gLightOnKnown = false;
    gBriKnown = false;
    gBriRid[0] = 0;
    return false;
  }
  if (!force && gBriLocal && gLightOnKnown && gBriRid[0] && strcmp(gBriRid, rid) == 0) {
    return false;
  }

  bool on = gLightOn;
  int pct = gBriPct;
  if (lightsDim) {
    bool got = false;
    bool anyOn = false;
    int firstOnPct = 0;
    int fallbackPct = pct;
    for (uint8_t i = 0; i < p->dimLightCount && i < kMaxDimLights; i++) {
      if (!p->dimLights[i][0]) {
        continue;
      }
      bool lightOn = false;
      int bri = 0;
      if (!hueGetLightState("light", p->dimLights[i], &lightOn, dimRing ? &bri : nullptr)) {
        continue;
      }
      got = true;
      if (!anyOn) {
        fallbackPct = bri;
      }
      if (lightOn && !anyOn) {
        anyOn = true;
        firstOnPct = bri;
      }
    }
    if (!got) {
      return false;
    }
    on = anyOn;
    pct = anyOn ? firstOnPct : fallbackPct;
  } else {
    if (!rtype) {
      gLightOnKnown = false;
      gBriKnown = false;
      gBriRid[0] = 0;
      return false;
    }
    if (!hueGetLightState(rtype, rid, &on, dimRing ? &pct : nullptr)) {
      return false;
    }
  }
  recipeCopyField(gBriRid, sizeof(gBriRid), rid);
  const bool onChanged = !gLightOnKnown || on != gLightOn;
  const bool briChanged = dimRing && (!gBriKnown || uiClampPct(pct) != gBriPct);
  gLightOn = on;
  gLightOnKnown = true;
  if (dimRing) {
    gBriPct = uiClampPct(pct);
    gBriKnown = true;
  }
  gBriLocal = false;
  return onChanged || briChanged;
}

inline bool uiRefreshScene() {
  gSceneHave = false;
  gSceneName[0] = 0;
  const HueRecipe *r = recipesFindScene(pagesActiveId());
  if (!r) {
    return false;
  }
  if (gLightOnKnown && !gLightOn) {
    return true;
  }
  const int idx = recipeFindActiveScene(r);
  if (idx < 0) {
    return true;
  }
  recipeCopyField(gSceneName, sizeof(gSceneName), r->scenes[idx].name);
  gSceneHave = gSceneName[0] != 0;
  return true;
}

inline void uiApplyLastSceneName() {
  gSceneHave = false;
  gSceneName[0] = 0;
  const HueRecipe *r = recipesFindScene(pagesActiveId());
  const char *rid = pagesLastSceneRid();
  if (!r || !rid || !rid[0]) {
    return;
  }
  for (uint8_t i = 0; i < r->sceneCount; i++) {
    if (strcmp(r->scenes[i].rid, rid) == 0) {
      recipeCopyField(gSceneName, sizeof(gSceneName), r->scenes[i].name);
      gSceneHave = gSceneName[0] != 0;
      return;
    }
  }
}

inline void uiLoadLevel() {
  const char *id = pagesActiveId();
  if (id && id[0] && !hueJobArmRefresh(id)) {
    gNeedHueState = true;
  }
}

inline void uiSyncFace() {
  if (gUi != UI_READY && gUi != UI_EMPTY) {
    return;
  }
  uiDrawReadyFace();
  if (uiHasDim()) {
    gBriShown = -1;
    uiDrawLevel();
  }
}

inline void uiSetLightOn(bool on) {
  const bool changed = !gLightOnKnown || on != gLightOn;
  gLightOn = on;
  gLightOnKnown = true;
  if (!on) {
    gSceneHave = false;
    gSceneName[0] = 0;
  }
  if (changed) {
    uiSyncFace();
  }
}

inline void uiHueJobPoll() {
  HueJobResult r;
  if (!hueJobTakeResult(&r)) {
    return;
  }
  if (strcmp(r.pageId, pagesActiveId()) != 0) {
    return;
  }
  if (r.kind == HUE_JOB_REFRESH) {
    if (gUi != UI_READY && gUi != UI_EMPTY) {
      return;
    }
    bool paint = false;
    if (r.haveTapOn && (!gTapOnKnown || r.tapOn != gTapOn)) {
      gTapOn = r.tapOn;
      gTapOnKnown = true;
      paint = true;
    } else if (r.haveTapOn) {
      gTapOnKnown = true;
    }
    if (r.haveDblOn && (!gDblOnKnown || r.dblOn != gDblOn)) {
      gDblOn = r.dblOn;
      gDblOnKnown = true;
      paint = true;
    } else if (r.haveDblOn) {
      gDblOnKnown = true;
    }
    if (r.haveTapOn || r.haveDblOn) {
      const bool any = (gTapOnKnown && gTapOn) || (gDblOnKnown && gDblOn);
      if (!gLightOnKnown || any != gLightOn) {
        gLightOn = any;
        paint = true;
      }
      gLightOnKnown = true;
    } else if (r.haveOn && (!gLightOnKnown || r.on != gLightOn)) {
      gLightOn = r.on;
      gLightOnKnown = true;
      if (!r.on) {
        gSceneHave = false;
        gSceneName[0] = 0;
      }
      paint = true;
    } else if (r.haveOn) {
      gLightOnKnown = true;
    }
    if (r.haveBri && !gBriLocal && uiHasDim()) {
      const int pct = uiClampPct(r.pct);
      if (!gBriKnown || pct != gBriPct) {
        gBriPct = pct;
        paint = true;
      }
      gBriKnown = true;
    }
    if (r.haveScene && (gLightOnKnown && gLightOn)) {
      if (r.sceneHave != gSceneHave || strcmp(gSceneName, r.sceneName) != 0) {
        gSceneHave = r.sceneHave;
        recipeCopyField(gSceneName, sizeof(gSceneName), r.sceneName);
        paint = true;
      }
    }
    if (paint) {
      uiSyncFace();
    }
    return;
  }
  if (!r.ok && (gUi == UI_READY || gUi == UI_EMPTY)) {
    gUiPressed = false;
    uiSet(UI_ERROR);
    uiPaint();
  }
}

inline void uiOnPageChanged() {
  gUiPressed = false;
  gLightOnKnown = false;
  gTapOn = false;
  gTapOnKnown = false;
  gDblOn = false;
  gDblOnKnown = false;
  gBriKnown = false;
  gBriLocal = false;
  gBriRid[0] = 0;
  gBriShown = -1;
  gBriLastSent = -1;
  gSceneHave = false;
  gSceneName[0] = 0;
  hueJobClearPending();
  gNeedHueState = true;
  const Page *p = pagesActive();
  LOG("page %u/%u %s\n", gPageIndex + 1, gPageCount, p ? p->name : "");
  if (gUi == UI_READY || gUi == UI_EMPTY) {
    uiPaint();
  }
}

inline bool uiFireEvent(const char *event) {
  if (gUi == UI_WIFI || gUi == UI_WIFI_FAIL || gUi == UI_NO_BRIDGE || gUi == UI_LOADING || gUi == UI_PAIRING ||
      gUi == UI_BOOT) {
    return false;
  }
  const bool split = uiSplitTwoLights();
  bool on = gLightOn;
  if (split) {
    if (event && strcmp(event, "double_click") == 0) {
      on = gDblOnKnown && gDblOn;
    } else {
      on = gTapOnKnown && gTapOn;
    }
  }
  const HueArmResult fr = hueJobArmRecipe(pagesActiveId(), event, &on);
  if (fr == HUE_ARM_ERR) {
    gUiPressed = false;
    uiSet(UI_ERROR);
    uiPaint();
    return false;
  }
  if (fr == HUE_ARM_NONE) {
    return true;
  }
  if (split) {
    if (event && strcmp(event, "double_click") == 0) {
      gDblOn = on;
      gDblOnKnown = true;
    } else {
      gTapOn = on;
      gTapOnKnown = true;
    }
    gLightOn = (gTapOnKnown && gTapOn) || (gDblOnKnown && gDblOn);
    gLightOnKnown = true;
    uiSyncFace();
    return true;
  }
  uiSetLightOn(on);
  if (on) {
    uiApplyLastSceneName();
    uiSyncFace();
  }
  return true;
}

inline void uiTick(unsigned long now) {
  uiHueJobPoll();
  if (gDimDragging) {
    return;
  }
  if (gTapWaitDouble && !gTouchDown && !gSecondTap && (now - gTapWaitMs) >= kDoubleTapMs) {
    gTapWaitDouble = false;
    uiFireEvent("short");
  }
  if (gUi == UI_ERROR && now >= gUiErrorUntilMs) {
    uiSet(uiFromRecipes());
  }
  if (gNeedFullPaint) {
    gNeedFullPaint = false;
    uiPaint();
  }
  if (gUi != gUiPainted) {
    if (gUi == UI_READY) {
      gNeedHueState = true;
    }
    uiPaint();
    return;
  }
  if (uiNeedsPulse(gUi) && (now - gUiPulseMs) >= 180) {
    uiDrawRings(now);
    gUiPulseMs = now;
  }
  if ((gUi == UI_READY || gUi == UI_EMPTY) && !gTouchDown) {
    if (gNeedHueState) {
      if (hueJobArmRefresh(pagesActiveId())) {
        gNeedHueState = false;
        gLightPollMs = now;
      }
    } else if (now - gLightPollMs >= 20000) {
      if (hueJobArmRefresh(pagesActiveId())) {
        gLightPollMs = now;
      }
    }
  }
}

inline bool uiDimPut(int pct) {
  const Page *p = pagesActive();
  if (!p || !uiHasDim()) {
    return false;
  }
  pct = uiClampPct(pct);
  if (pct == gBriLastSent) {
    return true;
  }
  if (!hueJobArmDim(p->id, pct)) {
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
    if (uiSplitTwoLights()) {
      gTapOn = true;
      gTapOnKnown = true;
      gDblOn = true;
      gDblOnKnown = true;
    }
    uiDrawReadyFace();
    gBriShown = -1;
  }
  if (pct != gBriShown) {
    uiDrawLevel();
  }
}

inline bool uiTrySwipe(int16_t x, int16_t y) {
  if (gSwipeDone || gPageCount <= 1 || gTouchMode != TOUCH_CENTER) {
    return false;
  }
  const int16_t dx = (int16_t)(x - gTouchStartX);
  const int16_t dy = (int16_t)(y - gTouchStartY);
  if (gPageSwipeAxis == PAGE_SWIPE_VERTICAL) {
    if (abs(dy) < kSwipeMinPx) {
      return false;
    }
    gSwipeDone = true;
    gTapWaitDouble = false;
    gSecondTap = false;
    if (dy < 0) {
      pagesNext();
    } else {
      pagesPrev();
    }
  } else {
    if (abs(dx) < kSwipeMinPx) {
      return false;
    }
    gSwipeDone = true;
    gTapWaitDouble = false;
    gSecondTap = false;
    if (dx < 0) {
      pagesNext();
    } else {
      pagesPrev();
    }
  }
  uiOnPageChanged();
  return true;
}

inline void uiTouchEnd(unsigned long now) {
  if (gDimDragging) {
    if (gBriKnown) {
      uiDimPut(gBriPct);
    }
    LOG("dim %d%%\n", gBriPct);
    gDimDragging = false;
    gDimHavePct = false;
  }

  const bool wasCenter = gTouchMode == TOUCH_CENTER;
  const bool swiped = gSwipeDone;
  gTouchDown = false;
  gUiPressed = false;
  gTouchMode = TOUCH_IDLE;
  gSwipeDone = false;
  gTouchIgnoreUntil = now + 40;

  if (wasCenter && !swiped && (gUi == UI_READY || gUi == UI_EMPTY)) {
    if (gSecondTap) {
      gSecondTap = false;
      gTapWaitDouble = false;
      uiFireEvent("double_click");
    } else if (uiPageHasDouble()) {
      gTapWaitDouble = true;
      gTapWaitMs = now;
    } else {
      uiFireEvent("short");
    }
  } else {
    gSecondTap = false;
  }

  if (gUi == UI_READY || gUi == UI_EMPTY) {
    uiDrawReadyFace();
    if (uiHasDim()) {
      uiDrawLevel();
    }
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
    const bool inBtn = touchHitButton(x, y, kBtnRadius);
    const bool inRing = touchHitRing(x, y, kRingGrabInner, kRingOuter + 8);
    if (gUi != UI_READY && gUi != UI_EMPTY) {
      return;
    }
    if (gTapWaitDouble && inRing && uiHasDim()) {
      gTapWaitDouble = false;
      uiFireEvent("short");
    }
    gTouchDown = true;
    gTouchLastPtMs = now;
    gTouchX = x;
    gTouchY = y;
    gTouchStartX = x;
    gTouchStartY = y;
    gSwipeDone = false;
    if (inRing && uiHasDim()) {
      gTouchMode = TOUCH_RING;
      gDimDragging = true;
      gDimHavePct = false;
      gSecondTap = false;
      LOG("ring raw %u,%u xy %d,%d full=%d\n", gTouchRawX, gTouchRawY, x, y,
                    (int)gTouchFullRange);
      uiDimFromPoint(x, y);
      return;
    }
    if (inBtn) {
      gTouchMode = TOUCH_CENTER;
      if (gTapWaitDouble) {
        gSecondTap = true;
      }
      gUiPressed = true;
      uiDrawRings(now);
      return;
    }
    gTouchMode = TOUCH_IDLE;
    gTouchDown = false;
    return;
  }

  if (moved) {
    gTouchLastPtMs = now;
    gTouchX = x;
    gTouchY = y;
    if (gDimDragging) {
      uiDimFromPoint(x, y);
    } else if (gTouchMode == TOUCH_CENTER) {
      uiTrySwipe(x, y);
    }
  } else if (irq) {
    gTouchLastPtMs = now;
  }
}
