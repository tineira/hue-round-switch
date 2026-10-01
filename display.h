#pragma once

#include <Arduino_GFX_Library.h>
#include <SPI.h>
#include "driver/gpio.h"
#include "json_util.h"

// Round Display for XIAO: GC9A01 240×240. Seeed pins (no global User_Setup).

static const int16_t kScreenW = 240;
static const int16_t kScreenH = 240;
static const int16_t kScreenCx = 120;
static const int16_t kScreenCy = 120;
static const int kPinLcdDc = D3;
static const int kPinLcdCs = D1;
static const int kPinLcdBl = D6;
static const int32_t kLcdSpiHz = 20000000;

inline Arduino_DataBus *gLcdBus = nullptr;
inline Arduino_GFX *gLcd = nullptr;
inline bool gDisplayOk = false;

inline constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// D6 = GPIO43 = U0TXD. Pull-up R18 on the Round: if the pad floats, the backlight stays ON.
// analogWrite (LEDC) + hold so the UART does not drive it HIGH again.
inline void displayBl(bool on) {
  const gpio_num_t pin = static_cast<gpio_num_t>(kPinLcdBl);
  gpio_hold_dis(pin);
  pinMode(kPinLcdBl, OUTPUT);
  gpio_set_drive_capability(pin, GPIO_DRIVE_CAP_3);
  analogWrite(kPinLcdBl, on ? 255 : 0);
  if (!on) {
    gpio_hold_en(pin);
  }
}

inline bool displayBegin() {
  displayBl(true);

  if (!gLcdBus) {
    gLcdBus = new Arduino_ESP32SPI(kPinLcdDc, kPinLcdCs, SCK, MOSI, MISO, FSPI);
  }
  if (!gLcd) {
    gLcd = new Arduino_GC9A01(gLcdBus, GFX_NOT_DEFINED, 0 /* rotation */, true /* IPS */);
  }
  if (!gLcd->begin(kLcdSpiHz)) {
    LOGLN("display begin failed");
    gDisplayOk = false;
    return false;
  }
  gLcd->fillScreen(RGB565_BLACK);
  gDisplayOk = true;
  return true;
}

inline void displayIdlePanel() {
  displayBl(false);
  if (gDisplayOk && gLcd) {
    gLcd->fillScreen(RGB565_BLACK);
    gLcd->displayOff();
  }
  displayBl(false);
}

inline void displayWakePanel() {
  displayBl(true);
  if (gDisplayOk && gLcd) {
    gLcd->displayOn();
  }
}

// Text arrives as UTF-8 (names keep ñ, é, ...). The built-in font draws one CP437 byte per
// glyph, so every string goes through circleToCp437 before it is measured or printed.
static const size_t kDisplayTextMax = 64;

// s is already in font bytes (CP437).
inline void displayTextCenterCp437(const char *s, int16_t cy, uint8_t size, uint16_t color) {
  if (!gDisplayOk || !gLcd || !s) {
    return;
  }
  gLcd->setTextSize(size);
  gLcd->setTextColor(color);
  int16_t x1 = 0, y1 = 0;
  uint16_t w = 0, h = 0;
  gLcd->getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  const int16_t x = kScreenCx - (int16_t)w / 2 - x1;
  const int16_t y = cy - (int16_t)h / 2 - y1;
  gLcd->setCursor(x, y);
  gLcd->print(s);
}

inline void displayTextCenter(const char *s, int16_t cy, uint8_t size, uint16_t color) {
  if (!gDisplayOk || !gLcd || !s) {
    return;
  }
  char buf[kDisplayTextMax];
  circleToCp437(buf, sizeof(buf), s);
  displayTextCenterCp437(buf, cy, size, color);
}

// UTF-8 in. Truncates with '.' at the end, one character per byte after the CP437
// conversion. Returns false if not even minChars characters fit (skip the scene).
inline bool displayTextEllipsis(const char *s, int16_t cy, uint8_t size, uint16_t color, int16_t maxW,
                               uint8_t minChars = 1) {
  if (!gDisplayOk || !gLcd || !s || !s[0] || maxW < 6) {
    return false;
  }
  char buf[kDisplayTextMax];
  circleToCp437(buf, sizeof(buf), s);
  if (!buf[0]) {
    return false;
  }
  gLcd->setTextSize(size);
  int16_t x1 = 0, y1 = 0;
  uint16_t w = 0, h = 0;
  gLcd->getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
  if (minChars > 1 && maxW < (int16_t)minChars * 6 * (int16_t)size) {
    return false;
  }
  while (w > (uint16_t)maxW && strlen(buf) > 1) {
    const size_t n = strlen(buf);
    buf[n - 1] = 0;
    if (n >= 2) {
      buf[n - 2] = '.';
    }
    gLcd->getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
  }
  if (w > (uint16_t)maxW) {
    return false;
  }
  displayTextCenterCp437(buf, cy, size, color);
  return true;
}
