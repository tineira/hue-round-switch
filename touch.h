#pragma once

#include <Wire.h>
#include "display.h"

// Touch Seeed Round Display: CHSC6X en 0x2E, INT en D7.
// Si el módulo trae CST816S (0x15) el scan lo elige.

static const int kPinTouchInt = D7;
static const uint8_t kTouchChsc = 0x2E;
static const uint8_t kTouchCst = 0x15;

inline uint8_t gTouchAddr = kTouchChsc;
inline bool gTouchFound = false;

inline void touchScanI2c() {
  Serial.print("I2C:");
  gTouchFound = false;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.printf(" 0x%02x", a);
      if (a == kTouchChsc || a == kTouchCst) {
        gTouchAddr = a;
        gTouchFound = true;
      }
    }
  }
  Serial.println();
  if (!gTouchFound) {
    Serial.println("touch: no CHSC6X/CST816S - trying 0x2E anyway");
    gTouchAddr = kTouchChsc;
  } else {
    Serial.printf("touch addr 0x%02x\n", gTouchAddr);
  }
}

inline void touchBegin() {
  pinMode(kPinTouchInt, INPUT_PULLUP);
  Wire.begin(SDA, SCL);
  Wire.setClock(100000);
  delay(20);
  touchScanI2c();
}

inline bool touchIrqPressed() {
  if (digitalRead(kPinTouchInt) != LOW) {
    delay(1);
    if (digitalRead(kPinTouchInt) != LOW) {
      return false;
    }
  }
  return true;
}

inline bool touchReadXY(int16_t *x, int16_t *y) {
  if (!x || !y) {
    return false;
  }
  const uint8_t n = Wire.requestFrom((int)gTouchAddr, 7);
  if (n < 5) {
    return false;
  }
  uint8_t t[7] = {0};
  Wire.readBytes(t, n > 7 ? 7 : n);
  // CHSC6X: t[0]==1, x=t[2], y=t[4]
  if (t[0] == 0x01 && n >= 5) {
    *x = t[2];
    *y = t[4];
    return *x < kScreenW && *y < kScreenH;
  }
  // CST816S: points in t[2], x/y packed in t[3..6]
  if (n >= 7 && t[2]) {
    *x = ((t[3] & 0x0F) << 8) | t[4];
    *y = ((t[5] & 0x0F) << 8) | t[6];
    return *x < kScreenW && *y < kScreenH;
  }
  return false;
}

inline bool touchHitButton(int16_t x, int16_t y, int16_t radius) {
  const int32_t dx = (int32_t)x - kScreenCx;
  const int32_t dy = (int32_t)y - kScreenCy;
  return dx * dx + dy * dy <= (int32_t)radius * radius;
}
