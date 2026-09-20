#pragma once

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
  UI_PAIRING,
  UI_EMPTY,
  UI_READY,
  UI_BUSY,
  UI_ERROR,
};

static const int16_t kBtnRadius = 88;
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

inline const HueRecipe *uiC1Recipe() {
  const HueRecipe *r = recipesFind("c1", "short");
  if (!r) {
    r = recipesFind("c1", "on");
  }
  return r;
}

inline UiScreen uiFromRecipes() { return uiC1Recipe() ? UI_READY : UI_EMPTY; }

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
  gLcd->drawCircle(kScreenCx, kScreenCy, 118, ring);
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
  gLcd->drawCircle(kScreenCx, kScreenCy, 118, ring);

  displayTextCenter("1", 92, 4, gUi == UI_EMPTY || gUi == UI_WIFI || gUi == UI_BOOT ? kColMute : kColInk);

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
  } else if (gUi == UI_PAIRING) {
    displayTextCenter("on the Hue Bridge", 166, 1, kColMute);
  } else if (gUi == UI_EMPTY) {
    displayTextCenter("Channel 1", 166, 1, kColMute);
  } else if (gUi == UI_NO_BRIDGE) {
    displayTextCenter("same LAN as Bridge", 166, 1, kColMute);
  }

  gUiPainted = gUi;
  gUiPulseMs = now;
}

inline bool uiNeedsPulse(UiScreen s) { return s == UI_PAIRING; }

inline void uiTick(unsigned long now) {
  if (gUi == UI_ERROR && now >= gUiErrorUntilMs) {
    uiSet(uiFromRecipes());
  }
  if (gUi != gUiPainted) {
    uiPaint();
    return;
  }
  if (uiNeedsPulse(gUi) && (now - gUiPulseMs) >= 180) {
    uiDrawRings(now);
    gUiPulseMs = now;
  }
}

inline bool uiOnTap() {
  if (gUi == UI_WIFI || gUi == UI_WIFI_FAIL || gUi == UI_NO_BRIDGE || gUi == UI_PAIRING || gUi == UI_BOOT) {
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

inline void uiPollTouch(unsigned long now) {
  const bool irq = touchIrqPressed();
  int16_t x = 0, y = 0;
  const bool hasPt = irq && touchReadXY(&x, &y);
  const bool inBtn = hasPt && touchHitButton(x, y, kBtnRadius);

  if (inBtn && !gTouchDown) {
    gTouchDown = true;
    gTouchDownMs = now;
    if (gUi == UI_READY || gUi == UI_EMPTY) {
      if (now - gTouchLastFireMs >= 250) {
        gTouchLastFireMs = now;
        gUiPressed = true;
        uiDrawRings(now);
        uiOnTap();
      } else {
        gUiPressed = true;
        uiDrawRings(now);
      }
    }
    return;
  }

  if (gTouchDown && !irq) {
    gTouchDown = false;
    gUiPressed = false;
    if (gUi == UI_READY || gUi == UI_EMPTY) {
      uiDrawRings(now);
    }
  }
}
