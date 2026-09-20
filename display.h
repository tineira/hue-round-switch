#pragma once

#include <Arduino_GFX_Library.h>
#include <SPI.h>

// Round Display for XIAO: GC9A01 240×240. Pines Seeed (no User_Setup global).

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

inline bool displayBegin() {
  pinMode(kPinLcdBl, OUTPUT);
  digitalWrite(kPinLcdBl, HIGH);

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

inline void displayBl(bool on) {
  pinMode(kPinLcdBl, OUTPUT);
  digitalWrite(kPinLcdBl, on ? HIGH : LOW);
}

// Reposo: panel negro y BL D6 off. Sleep del GC9A01 no hace falta en v1.
inline void displayIdlePanel() {
  if (gDisplayOk && gLcd) {
    gLcd->fillScreen(RGB565_BLACK);
  }
  displayBl(false);
}

inline void displayWakePanel() { displayBl(true); }

inline void displayTextCenter(const char *s, int16_t cy, uint8_t size, uint16_t color) {
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

// Recorta con '.' al final. Devuelve false si ni 8 caracteres entran (omitir escena).
inline bool displayTextEllipsis(const char *s, int16_t cy, uint8_t size, uint16_t color, int16_t maxW,
                               uint8_t minChars = 1) {
  if (!gDisplayOk || !gLcd || !s || !s[0] || maxW < 6) {
    return false;
  }
  char buf[40];
  strncpy(buf, s, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = 0;
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
  displayTextCenter(buf, cy, size, color);
  return true;
}
