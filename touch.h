#pragma once

#include <Wire.h>
#include "display.h"

// Seeed Round Display touch: CHSC6X at 0x2E, INT on D7.
// If the module has a CST816S (0x15), the scan picks it.

static const int kPinTouchInt = D7;
static const uint8_t kTouchChsc = 0x2E;
static const uint8_t kTouchCst = 0x15;

inline uint8_t gTouchAddr = kTouchChsc;
inline bool gTouchFound = false;
inline bool gTouchFullRange = false;  // true if the chip reports 0..239
inline uint8_t gTouchRawX = 0;
inline uint8_t gTouchRawY = 0;
inline uint8_t gTouchWideFrames = 0;
static const uint8_t kTouchWideFramesNeeded = 3;

inline void touchScanI2c() {
  LOGS("I2C:");
  gTouchFound = false;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      LOG(" 0x%02x", a);
      if (a == kTouchChsc || a == kTouchCst) {
        gTouchAddr = a;
        gTouchFound = true;
      }
    }
  }
  LOGLN("");
  if (!gTouchFound) {
    LOGLN("touch: no CHSC6X/CST816S - trying 0x2E anyway");
    gTouchAddr = kTouchChsc;
  } else {
    LOG("touch addr 0x%02x\n", gTouchAddr);
  }
}

inline void touchBegin() {
  pinMode(kPinTouchInt, INPUT_PULLUP);
  Wire.begin(SDA, SCL);
  Wire.setClock(100000);
  delay(20);
  touchScanI2c();
}

inline bool touchIrqPressed() { return digitalRead(kPinTouchInt) == LOW; }

// The chip reports 0..127 or 0..239. Switch to full range only after a few consecutive
// frames above 127, never on one corrupted frame; frames beyond the panel are dropped.
inline bool touchMapRaw(uint8_t rawx, uint8_t rawy, int16_t *x, int16_t *y) {
  gTouchRawX = rawx;
  gTouchRawY = rawy;
  if (rawx >= kScreenW || rawy >= kScreenH) {
    return false;
  }
  if (!gTouchFullRange) {
    if (rawx > 127 || rawy > 127) {
      if (++gTouchWideFrames < kTouchWideFramesNeeded) {
        return false;
      }
      gTouchFullRange = true;
      LOGLN("touch: full range 0..239");
    } else {
      gTouchWideFrames = 0;
    }
  }
  if (!gTouchFullRange) {
    *x = (int16_t)((int32_t)rawx * (kScreenW - 1) / 127);
    *y = (int16_t)((int32_t)rawy * (kScreenH - 1) / 127);
  } else {
    *x = rawx;
    *y = rawy;
  }
  return true;
}

inline bool touchReadXY(int16_t *x, int16_t *y) {
  if (!x || !y) {
    return false;
  }
  const uint8_t n = Wire.requestFrom((int)gTouchAddr, 5);
  if (n < 5) {
    return false;
  }
  uint8_t t[5] = {0};
  Wire.readBytes(t, 5);
  // CHSC6X Seeed: t[0]==1, x=t[2], y=t[4] (sometimes 0..127, not 0..239)
  if (t[0] == 0x01) {
    return touchMapRaw(t[2], t[4], x, y);
  }
  return false;
}

inline int32_t touchR2(int16_t x, int16_t y) {
  const int32_t dx = (int32_t)x - kScreenCx;
  const int32_t dy = (int32_t)y - kScreenCy;
  return dx * dx + dy * dy;
}

inline bool touchHitButton(int16_t x, int16_t y, int16_t radius) {
  return touchR2(x, y) <= (int32_t)radius * radius;
}

inline bool touchHitRing(int16_t x, int16_t y, int16_t inner, int16_t outer) {
  const int32_t r2 = touchR2(x, y);
  return r2 >= (int32_t)inner * inner && r2 <= (int32_t)outer * outer;
}
