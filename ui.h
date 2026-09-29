#pragma once

#include <math.h>
#include "channels.h"
#include "display.h"
#include "hue_job.h"
#include "pages.h"
#include "recipes.h"
#include "touch.h"

// English copy. Ready does not teach gestures.

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
  UI_TOKEN,
  UI_UPDATING,  // Update over Wi-Fi (ota.h); touches wait for the restart
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
// Counted from the detected lift. The lift is now seen ~30 ms after the finger leaves
// (was 80-400 ms), so the window grew to keep slow double taps reaching it.
static const unsigned long kDoubleTapMs = 400;
// INT (D7) stays low while a finger is on the glass; Seeed's driver reads INT high as
// released. 30 ms of INT high ends the touch, even if the chip still repeats the last
// point. A fast double tap is then two touches, not one long press.
static const unsigned long kTouchLiftMs = 30;
static const unsigned long kTouchRearmMs = 15;
// A touch that stays down with no movement this long is taken as a stuck report (INT
// held low, the chip repeating its last point), not a finger. No gesture here needs a
// still finger for more than a few seconds: tap and double tap fire on lift, a swipe
// moves, and the ring commits its level on lift. While a touch is down the console
// poll, the idle timeout and Hue state reads wait, so a stuck touch must end.
static const unsigned long kTouchStuckMs = 60000;
static const int16_t kSwipeMinPx = 40;
static const uint16_t kColYellow = 0xFFE0;
static const int16_t kTokenMarkY = 229;  // In the ring's gap at 6 o'clock
static const unsigned long kStateFreshMs = 5000;  // Older on/off state: a toggle asks the Bridge first
static const unsigned long kFlashMs = 700;
static const unsigned long kRefreshAfterCmdMs = 1500;
static const unsigned long kLightPollMs = 20000;

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
inline char gBriRid[40] = {0};
inline int gBriShown = -1;
inline int gBriLastSent = -1;
inline bool gLightOn = false;
inline bool gLightOnKnown = false;
inline bool gTapOn = false;
inline bool gTapOnKnown = false;
inline bool gDblOn = false;
inline bool gDblOnKnown = false;
inline unsigned long gLightPollMs = 0;
// When each on/off value was last confirmed by the Bridge or set by a command here.
inline unsigned long gLightStateMs = 0;
inline unsigned long gTapStateMs = 0;
inline unsigned long gDblStateMs = 0;
inline bool gUiFlashing = false;
inline unsigned long gUiFlashUntil = 0;
inline bool gRefreshAt = false;
inline unsigned long gRefreshAtMs = 0;
inline unsigned long gTouchLastPtMs = 0;
inline unsigned long gTouchIrqLowMs = 0;
inline unsigned long gTouchIgnoreUntil = 0;
inline unsigned long gTouchStillSinceMs = 0;  // Last touch start or movement
inline bool gTouchStuck = false;  // Ended by kTouchStuckMs; ignore touch until INT goes high
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
inline bool gScreenIdle = false;  // Backlight off; the first touch only wakes
inline bool gIdleWakeHold = false;
inline unsigned long gIdleLastMs = 0;

inline UiScreen uiFromRecipes() { return gPageCount > 0 ? UI_READY : UI_EMPTY; }

inline bool uiPageHasDouble() { return recipesFind(pagesActiveId(), "double_click") != nullptr; }

inline bool uiHasDim() { return pagesHasDim(); }

inline bool uiSplitTwoLights() { return recipesTwoChildLights(pagesActiveId(), nullptr, nullptr); }

inline bool uiLightLit() {
  if (uiSplitTwoLights()) {
    return (gTapOnKnown && gTapOn) || (gDblOnKnown && gDblOn);
  }
  return gLightOnKnown && gLightOn;
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

// fillArc: 0° = 3 o'clock, clockwise. The brightness ring runs from 135° to 135+270°.
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

inline void uiPaint();
inline void otaPaintScreen();  // ota.h

inline void uiIdleEnter() {
  if (gScreenIdle) {
    return;
  }
  gScreenIdle = true;
  gIdleWakeHold = false;
  gTapWaitDouble = false;
  gSecondTap = false;
  gUiPressed = false;
  displayIdlePanel();
  pagesFlushLazy(true);
  LOG("display idle\n");
}

inline void uiIdleWake(unsigned long now) {
  displayWakePanel();
  gScreenIdle = false;
  gIdleWakeHold = true;
  gNeedFullPaint = false;
  gNeedHueState = true;
  gIdleLastMs = now;
  gTapWaitDouble = false;
  gSecondTap = false;
  uiPaint();
  LOG("display wake\n");
}

inline void uiIdleNoteTouch(unsigned long now) { gIdleLastMs = now; }

inline void uiSet(UiScreen s) {
  if (s == UI_ERROR) {
    gUiErrorUntilMs = millis() + 2000;
  }
  // The Updating screen starts asleep when the screen was (ota-round spec §4.2).
  if (gScreenIdle && s != UI_READY && s != UI_EMPTY && s != UI_UPDATING) {
    displayWakePanel();
    gScreenIdle = false;
    gIdleWakeHold = false;
  }
  if (gUi != s) {
    gUi = s;
    if (s == UI_READY || s == UI_EMPTY) {
      gIdleLastMs = millis();
    }
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
    case UI_TOKEN:
      return t->error;
    case UI_EMPTY:
      return t->mute;
    case UI_READY:
      if (gUiFlashing) {
        return t->error;
      }
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
  if (!gDisplayOk || !gLcd || gScreenIdle) {
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
  // Console token rejected: a quiet marker; the switch keeps working from NVS.
  if (gConsoleAuthRejected) {
    gLcd->fillCircle(kScreenCx, kTokenMarkY, 4, t->error);
  }
}

inline void uiPaint() {
  if (gScreenIdle && (gUi == UI_READY || gUi == UI_EMPTY || gUi == UI_UPDATING)) {
    return;
  }
  if (!gDisplayOk || !gLcd) {
    gUiPainted = gUi;
    return;
  }

  const unsigned long now = millis();
  const PageTheme *t = uiTheme();
  gLcd->fillScreen(t->bg);

  if (gUi == UI_UPDATING) {
    otaPaintScreen();
    gUiPainted = gUi;
    gUiPulseMs = now;
    return;
  }
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
    case UI_TOKEN:
      line = "Token rejected";
      break;
    case UI_UPDATING:
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
  } else if (gUi == UI_TOKEN) {
    displayTextCenter("Set a new one in Setup", 166, 1, t->mute);
  }

  if (gUi == UI_READY) {
    gBriShown = -1;
    uiDrawLevel();
  }

  gUiPainted = gUi;
  gUiPulseMs = now;
}

inline bool uiNeedsPulse(UiScreen s) { return s == UI_PAIRING || s == UI_LOADING; }

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
  if (id && id[0] && !hueJobArmRefresh(id, true)) {
    gNeedHueState = true;
  }
}

inline void uiSyncFace() {
  if (gScreenIdle || (gUi != UI_READY && gUi != UI_EMPTY)) {
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
  gLightStateMs = millis();
  if (!on) {
    gSceneHave = false;
    gSceneName[0] = 0;
  }
  if (changed) {
    uiSyncFace();
  }
}

// A command failed: the button ring flashes the error color; Ready stays and keeps
// taking touches.
inline void uiFlashError() {
  gUiFlashing = true;
  gUiFlashUntil = millis() + kFlashMs;
  if (!gScreenIdle && gUi == UI_READY) {
    uiDrawRings(millis());
  }
}

inline void uiRefreshSoon(unsigned long delayMs) {
  gRefreshAt = true;
  gRefreshAtMs = millis() + delayMs;
}

inline void uiApplyUserResult(const HueJobResult &r) {
  const int pageIdx = pagesIndexOf(r.pageId);
  // Scene cursor bookkeeping runs even when this result is not painted (newer tap, other page).
  if (r.isScene && pageIdx >= 0) {
    const bool cursorOurs = strcmp(pagesSceneRidAt(pageIdx), r.chosenRid) == 0;
    if (!r.ok && cursorOurs) {
      pagesSetSceneRidAt(pageIdx, r.prevRid);
    } else if (r.ok && cursorOurs && r.sceneRid[0] && strcmp(r.sceneRid, r.chosenRid) != 0) {
      pagesSetSceneRidAt(pageIdx, r.sceneRid);  // 404 skipped to the next scene
    }
  }
  if (pageIdx < 0 || pageIdx != gPageIndex || r.gen != hueJobGen()) {
    return;
  }
  if (gUi != UI_READY && gUi != UI_EMPTY) {
    return;
  }
  if (!r.ok) {
    uiFlashError();
    if (r.isScene) {
      uiApplyLastSceneName();
      uiSyncFace();
    }
    uiRefreshSoon(300);
    return;
  }
  const unsigned long now = millis();
  bool paint = false;
  if (r.haveOn) {
    const bool split = uiSplitTwoLights();
    if (split && r.kind == HUE_JOB_RECIPE) {
      if (strcmp(r.event, "double_click") == 0) {
        paint = paint || !gDblOnKnown || gDblOn != r.on;
        gDblOn = r.on;
        gDblOnKnown = true;
        gDblStateMs = now;
      } else {
        paint = paint || !gTapOnKnown || gTapOn != r.on;
        gTapOn = r.on;
        gTapOnKnown = true;
        gTapStateMs = now;
      }
      gLightOn = (gTapOnKnown && gTapOn) || (gDblOnKnown && gDblOn);
      gLightOnKnown = true;
    } else {
      paint = paint || !gLightOnKnown || gLightOn != r.on;
      gLightOn = r.on;
      gLightOnKnown = true;
      gLightStateMs = now;
      if (!r.on) {
        paint = paint || gSceneHave;
        gSceneHave = false;
        gSceneName[0] = 0;
        if (!split) {
          pagesSetLastSceneRid("");
        }
      } else if (!r.isScene && !gSceneHave) {
        uiApplyLastSceneName();
        paint = paint || gSceneHave;
      }
    }
  }
  if (r.isScene && r.haveScene) {
    paint = paint || gSceneHave != r.sceneHave || strcmp(gSceneName, r.sceneName) != 0;
    gSceneHave = r.sceneHave;
    recipeCopyField(gSceneName, sizeof(gSceneName), r.sceneName);
  }
  if (paint && !gScreenIdle) {
    uiSyncFace();
  }
  // The Bridge is the truth: read it back once the transition settled.
  uiRefreshSoon(kRefreshAfterCmdMs);
}

inline void uiApplyRefresh(const HueJobResult &r) {
  // A command since this read was queued: its state is newer than the read.
  if (strcmp(r.pageId, pagesActiveId()) != 0 || r.gen != hueJobGen()) {
    return;
  }
  if (gUi != UI_READY && gUi != UI_EMPTY) {
    return;
  }
  const unsigned long now = millis();
  bool paint = false;
  if (r.haveTapOn) {
    paint = paint || !gTapOnKnown || r.tapOn != gTapOn;
    gTapOn = r.tapOn;
    gTapOnKnown = true;
    gTapStateMs = now;
  }
  if (r.haveDblOn) {
    paint = paint || !gDblOnKnown || r.dblOn != gDblOn;
    gDblOn = r.dblOn;
    gDblOnKnown = true;
    gDblStateMs = now;
  }
  if (r.haveTapOn || r.haveDblOn) {
    const bool any = (gTapOnKnown && gTapOn) || (gDblOnKnown && gDblOn);
    paint = paint || !gLightOnKnown || any != gLightOn;
    gLightOn = any;
    gLightOnKnown = true;
    gLightStateMs = now;
  } else if (r.haveOn) {
    paint = paint || !gLightOnKnown || r.on != gLightOn;
    gLightOn = r.on;
    gLightOnKnown = true;
    gLightStateMs = now;
    if (!r.on) {
      paint = paint || gSceneHave;
      gSceneHave = false;
      gSceneName[0] = 0;
    }
  }
  if (r.haveBri && uiHasDim() && !gDimDragging) {
    const int pct = uiClampPct(r.pct);
    paint = paint || !gBriKnown || pct != gBriPct;
    gBriPct = pct;
    gBriKnown = true;
    gBriLastSent = pct;
  }
  if (r.haveScene && gLightOnKnown && gLightOn) {
    if (r.sceneHave != gSceneHave || strcmp(gSceneName, r.sceneName) != 0) {
      gSceneHave = r.sceneHave;
      recipeCopyField(gSceneName, sizeof(gSceneName), r.sceneName);
      paint = true;
    }
    // The next tap cycles on from the scene that is really on.
    if (r.sceneHave && r.sceneRid[0] && strcmp(pagesLastSceneRid(), r.sceneRid) != 0) {
      pagesSetLastSceneRid(r.sceneRid);
    }
  }
  if (paint && !gScreenIdle) {
    uiSyncFace();
  }
}

inline void uiHueJobPoll() {
  HueJobResult r;
  while (hueJobTakeResult(&r)) {
    if (r.kind == HUE_JOB_REFRESH) {
      uiApplyRefresh(r);
    } else {
      uiApplyUserResult(r);
    }
  }
}

inline void uiOnPageChanged() {
  gUiPressed = false;
  gLightOn = false;
  gLightOnKnown = false;
  gTapOn = false;
  gTapOnKnown = false;
  gDblOn = false;
  gDblOnKnown = false;
  gLightStateMs = 0;
  gTapStateMs = 0;
  gDblStateMs = 0;
  gBriKnown = false;
  gBriRid[0] = 0;
  gBriShown = -1;
  gBriLastSent = -1;
  gSceneHave = false;
  gSceneName[0] = 0;
  gRefreshAt = false;
  hueJobClearPending();
  gNeedHueState = true;
  const Page *p = pagesActive();
  LOG("page %u/%u %s\n", gPageIndex + 1, gPageCount, p ? p->name : "");
  if (!gScreenIdle && (gUi == UI_READY || gUi == UI_EMPTY)) {
    uiPaint();
  }
}

// Paints the gesture's local result at once (fill, scene name); the PUT follows in the
// hueJob task. Always repaints the face (clears the press invert).
inline bool uiFireEvent(const char *event) {
  if (gScreenIdle || gIdleWakeHold) {
    return false;
  }
  if (gUi != UI_READY && gUi != UI_EMPTY) {
    return false;
  }
  const unsigned long now = millis();
  const bool split = uiSplitTwoLights();
  const bool dbl = event && strcmp(event, "double_click") == 0;
  bool known = gLightOnKnown;
  bool on = gLightOn;
  unsigned long stateMs = gLightStateMs;
  if (split) {
    known = dbl ? gDblOnKnown : gTapOnKnown;
    on = dbl ? gDblOn : gTapOn;
    stateMs = dbl ? gDblStateMs : gTapStateMs;
  }
  const bool fresh = known && stateMs != 0 && (now - stateMs) < kStateFreshMs;
  HueArmInfo info;
  const HueArmResult fr = hueJobArmRecipe(pagesActiveId(), event, fresh, known && on, &info);
  if (fr == HUE_ARM_ERR) {
    uiSyncFace();
    uiFlashError();
    return false;
  }
  if (fr == HUE_ARM_NONE || !info.haveOn) {
    // No recipe, or a toggle the task resolves with a GET first: paint on the result.
    uiSyncFace();
    return true;
  }
  gRefreshAt = false;
  if (split) {
    if (dbl) {
      gDblOn = info.on;
      gDblOnKnown = true;
      gDblStateMs = now;
    } else {
      gTapOn = info.on;
      gTapOnKnown = true;
      gTapStateMs = now;
    }
    gLightOn = (gTapOnKnown && gTapOn) || (gDblOnKnown && gDblOn);
    gLightOnKnown = true;
    uiSyncFace();
    return true;
  }
  gLightOn = info.on;
  gLightOnKnown = true;
  gLightStateMs = now;
  if (!info.on) {
    gSceneHave = false;
    gSceneName[0] = 0;
  } else {
    // For a scene tap the cursor already points at the scene being recalled.
    uiApplyLastSceneName();
  }
  uiSyncFace();
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
  if (gUiFlashing && (long)(now - gUiFlashUntil) >= 0) {
    gUiFlashing = false;
    if (!gScreenIdle && gUi == UI_READY) {
      uiDrawRings(now);
    }
  }
  if (gUi == UI_ERROR && now >= gUiErrorUntilMs) {
    uiSet(uiFromRecipes());
  }
  if (!gScreenIdle && (gUi == UI_READY || gUi == UI_EMPTY) && gScreenTimeoutSec > 0 && !gTouchDown &&
      !gIdleWakeHold) {
    if (gIdleLastMs == 0) {
      gIdleLastMs = now;
    } else if (now - gIdleLastMs >= (unsigned long)gScreenTimeoutSec * 1000UL) {
      uiIdleEnter();
    }
  }
  if (gNeedFullPaint) {
    gNeedFullPaint = false;
    if (!gScreenIdle) {
      uiPaint();
    }
  }
  if (gUi != gUiPainted) {
    if (gScreenIdle && (gUi == UI_READY || gUi == UI_EMPTY || gUi == UI_UPDATING)) {
      displayBl(false);
      return;
    }
    if (gUi == UI_READY) {
      gNeedHueState = true;
    }
    uiPaint();
    return;
  }
  if (gScreenIdle) {
    // No Hue reads while asleep.
    displayBl(false);
    return;
  }
  if (uiNeedsPulse(gUi) && (now - gUiPulseMs) >= 180) {
    uiDrawRings(now);
    gUiPulseMs = now;
  }
  if ((gUi == UI_READY || gUi == UI_EMPTY) && !gTouchDown && hueLinkUsable()) {
    const char *id = pagesActiveId();
    if (gNeedHueState) {
      if (hueJobArmRefresh(id, true)) {
        gNeedHueState = false;
        gRefreshAt = false;
        gLightPollMs = now;
      }
    } else if (gRefreshAt && (long)(now - gRefreshAtMs) >= 0) {
      if (hueJobArmRefresh(id, true)) {
        gRefreshAt = false;
        gLightPollMs = now;
      }
    } else if (now - gLightPollMs >= kLightPollMs) {
      if (hueJobArmRefresh(id, true)) {
        gLightPollMs = now;
      }
    }
  }
}

inline bool uiDimPut(int pct) {
  if (gScreenIdle || gIdleWakeHold) {
    return false;
  }
  const Page *p = pagesActive();
  if (!p || !uiHasDim()) {
    return false;
  }
  pct = uiClampPct(pct);
  if (pct == gBriLastSent) {
    return true;
  }
  if (!hueJobArmDim(p->id, pct)) {
    uiFlashError();
    return false;
  }
  gRefreshAt = false;
  gBriLastSent = pct;
  gBriPct = pct;
  gBriKnown = true;
  uiSetLightOn(true);
  return true;
}

inline void uiDimFromPoint(int16_t x, int16_t y) {
  int pct = 0;
  if (!uiTouchToPct(x, y, &pct)) {
    return;
  }
  uiIdleNoteTouch(millis());
  if (gDimHavePct && abs(pct - gBriPct) > 18) {
    return;
  }
  gDimHavePct = true;
  gBriPct = pct;
  gBriKnown = true;
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
  if (gScreenIdle || gIdleWakeHold || gSwipeDone || gPageCount <= 1 || gTouchMode != TOUCH_CENTER) {
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
  uiIdleNoteTouch(now);
  if (gIdleWakeHold) {
    gIdleWakeHold = false;
    gTouchDown = false;
    gUiPressed = false;
    gTouchMode = TOUCH_IDLE;
    gSwipeDone = false;
    gDimDragging = false;
    gDimHavePct = false;
    gSecondTap = false;
    gTapWaitDouble = false;
    gTouchIgnoreUntil = now + kTouchRearmMs;
    return;
  }
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
  gTouchIgnoreUntil = now + kTouchRearmMs;

  bool fired = false;
  if (wasCenter && !swiped && (gUi == UI_READY || gUi == UI_EMPTY)) {
    if (gSecondTap) {
      gSecondTap = false;
      gTapWaitDouble = false;
      uiFireEvent("double_click");
      fired = true;
    } else if (uiPageHasDouble()) {
      gTapWaitDouble = true;
      gTapWaitMs = now;
    } else {
      uiFireEvent("short");
      fired = true;
    }
  } else {
    gSecondTap = false;
  }

  if (!fired && (gUi == UI_READY || gUi == UI_EMPTY)) {
    uiDrawReadyFace();
    if (uiHasDim()) {
      uiDrawLevel();
    }
  }
}

// End a touch that has been still for kTouchStuckMs. Nothing fires for the circle (a
// minute-long press is not a tap, and the pending first tap of a double tap is
// dropped); the ring keeps the level under the finger, as on a lift.
inline void uiTouchEndStuck(unsigned long now) {
  LOG("touch stuck %lums, released\n", now - gTouchStillSinceMs);
  if (gTouchMode == TOUCH_CENTER) {
    gTouchMode = TOUCH_IDLE;
  }
  gSecondTap = false;
  gTapWaitDouble = false;
  uiTouchEnd(now);
  gTouchStuck = true;
}

inline void uiPollTouch(unsigned long now) {
  const bool irq = touchIrqPressed();
  if (irq) {
    gTouchIrqLowMs = now;
  }
  if (gTouchStuck) {
    // After a stuck touch, only a real lift (INT high) re-arms the glass. Otherwise
    // the same stuck report would start a new touch, or wake the idle screen, at once.
    if (irq) {
      return;
    }
    gTouchStuck = false;
  }
  int16_t x = 0, y = 0;
  bool hasPt = false;
  if (irq || gTouchDown) {
    hasPt = touchReadXY(&x, &y);
  }
  const bool moved = hasPt && (abs(x - gTouchX) + abs(y - gTouchY) >= 2);

  if (gScreenIdle) {
    if (!irq && !hasPt) {
      return;
    }
    uiIdleWake(now);
    gTouchDown = true;
    gIdleWakeHold = true;
    gTouchLastPtMs = now;
    gTouchStillSinceMs = now;
    gTouchX = hasPt ? x : -1;
    gTouchY = hasPt ? y : -1;
    gTouchStartX = gTouchX;
    gTouchStartY = gTouchY;
    gSwipeDone = false;
    gDimDragging = false;
    gDimHavePct = false;
    gUiPressed = false;
    gTouchMode = TOUCH_IDLE;
    return;
  }

  if (gTouchDown) {
    if (!irq && (now - gTouchIrqLowMs) >= kTouchLiftMs) {
      LOG("lift int-high %lums\n", now - gTouchIrqLowMs);
      uiTouchEnd(now);
      return;
    }
    if (!hasPt && (now - gTouchLastPtMs) >= 80) {
      LOG("lift no-point %lums\n", now - gTouchLastPtMs);
      uiTouchEnd(now);
      return;
    }
    if (!moved && (now - gTouchStillSinceMs) >= kTouchStuckMs) {
      uiTouchEndStuck(now);
      return;
    }
  }

  if (!gTouchDown) {
    if (!irq || !hasPt || now < gTouchIgnoreUntil) {
      return;
    }
    // With a dimmer the ring starts right outside the button; without one the button
    // takes the whole face. No dead band either way.
    const bool hasDim = uiHasDim();
    const bool inBtn = touchHitButton(x, y, hasDim ? kBtnRadius : kRingOuter + 8);
    const bool inRing = hasDim && !inBtn && touchHitRing(x, y, kBtnRadius, kRingOuter + 8);
    if (gUi != UI_READY && gUi != UI_EMPTY) {
      return;
    }
    if (gTapWaitDouble && inRing) {
      gTapWaitDouble = false;
      uiFireEvent("short");
    }
    gTouchDown = true;
    gTouchLastPtMs = now;
    gTouchStillSinceMs = now;
    gTouchX = x;
    gTouchY = y;
    gTouchStartX = x;
    gTouchStartY = y;
    gSwipeDone = false;
    uiIdleNoteTouch(now);
    if (inRing) {
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
    gTouchStillSinceMs = now;
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
