#pragma once

// Update over Wi-Fi (console docs/specs/ota-round.md, on top of ota.md §4). Ported from the Simple
// switch's ota.h; shared per chip family only, so this copy is the Round's own.
//
// Console task (core 0): a poll's 200 body may carry `ota` { version, url, sha256, size }. The
// task parses it before handing the body to the loop and before the epoch check, whatever the rev.
// After the hand-off (the poll's connection is closed by then) it asks the loop for the Updating
// screen. The loop accepts only on a quiet Ready or Empty screen (otaLoopTick); otherwise the offer
// waits for the next poll. The task then downloads the app image and writes it to the inactive
// slot while hashing it. Only a matching length and sha256 reach Update.end(), which makes the slot
// bootable. NVS is never erased. The offer is not stored: it is applied from the poll that carried
// it, so a cancelled offer is simply absent on the next poll.
//
// Loop (core 1): paints the Updating screen and its ring from the task's progress, ignores touches
// until the restart, and after a failure shows "Update failed" for about 2 s before going back.
// An asleep screen stays asleep (a touch wakes it to the Updating screen).
//
// Confirming: verifyRollbackLater() (the .ino) keeps the core from confirming the image at
// startup; otaConfirm() confirms it after the first 200/204 poll. A restart before that makes the
// bootloader go back to the old slot. NVS ota/try holds the version being installed from just
// before the restart until it confirms; the old app finds it with the last invalid partition set
// and reports ota_error=boot once.
//
// Interrupted download: NVS ota/dl holds the version from just before the slot is first written
// until the attempt ends either way. Found at boot (power cut, crash or reset mid-download), it is
// reported as ota_error=size (fewer bytes than ota.size) and retried after an hour, like a stream
// that ended early.

#include <atomic>
#include <Update.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>
#include "json_util.h"
#include "ui.h"

struct OtaOffer {
  char version[16];
  char url[128];
  char sha256[65];
  uint32_t size;
};

// Retry a failed offer at most this often; size and sha block that version until reboot.
static const unsigned long kOtaRetryMs = 60UL * 60UL * 1000UL;
// Internal RAM needed to start (mbedtls allocates internal RAM on this core). Measured in the
// Downgrade test (ota-round spec §7): the TLS session plus the 4 KB write buffer took ~57 KB in
// all, its largest piece ~17 KB. Right after a restart the free RAM is fragmented (134 KB free,
// largest block 47 KB), so the check is on both, with room, not on one large block.
static const size_t kOtaMinFree = 80 * 1024;
static const size_t kOtaMinBlock = 24 * 1024;
static const unsigned long kOtaStallMs = 20000;
// The screen must have been left alone this long before an update starts.
static const unsigned long kOtaQuietMs = 10000;
static const unsigned long kOtaAskWaitMs = 2000;
static const unsigned long kOtaFailShowMs = 2000;

inline bool gOtaOfferValid = false;  // set by the poll that carried `ota`
inline OtaOffer gOtaOffer;
// Sent as ota_error on the next poll, then dropped once the console answered 200/204.
inline char gOtaErrorPending[12] = "";
inline char gOtaFailedVersion[16] = "";
inline unsigned long gOtaFailedMs = 0;
inline char gOtaBlockedVersion[16] = "";  // after size or sha: the image itself is bad
inline bool gOtaConfirmed = false;

// Console task ↔ loop. The task moves NONE → ASK and, once the loop took it, DOWNLOADING →
// RESTARTING or FAILED; the loop moves ASK → DOWNLOADING or DECLINED, and FAILED → NONE.
enum OtaUiPhase : uint8_t {
  OTA_UI_NONE = 0,
  OTA_UI_ASK,
  OTA_UI_DECLINED,
  OTA_UI_DOWNLOADING,
  OTA_UI_RESTARTING,
  OTA_UI_FAILED,
};

inline std::atomic<uint8_t> gOtaUiPhase{OTA_UI_NONE};
inline std::atomic<uint8_t> gOtaUiPct{0};
inline char gOtaUiVersion[16] = "";  // written by the task before ASK, read by the loop after

// Loop only.
inline bool gOtaWasAsleep = false;
inline int gOtaShownPct = -1;
inline uint8_t gOtaShownPhase = OTA_UI_NONE;
inline unsigned long gOtaFailUntilMs = 0;

// ---------------------------------------------------------------- boot, poll (console task)

inline void otaFail(const char *version, const char *code, bool block) {
  LOG("ota %s failed: %s\n", version, code);
  snprintf(gOtaErrorPending, sizeof(gOtaErrorPending), "%s", code);
  snprintf(gOtaFailedVersion, sizeof(gOtaFailedVersion), "%s", version);
  gOtaFailedMs = millis();
  if (block) {
    snprintf(gOtaBlockedVersion, sizeof(gOtaBlockedVersion), "%s", version);
  }
}

inline void otaMarkDownload(const char *version) {
  Preferences p;
  if (p.begin("ota", false)) {
    if (version) {
      p.putString("dl", version);
    } else if (p.isKey("dl")) {
      p.remove("dl");
    }
    p.end();
  }
}

// Setup: a download cut off by a restart, or a restart before the new image confirmed itself
// (the bootloader then went back to this one).
inline void otaBootCheck() {
  Preferences p;
  if (!p.begin("ota", true)) {
    return;  // namespace never written: no update tried
  }
  const String dl = p.getString("dl", "");
  const String tried = p.getString("try", "");
  p.end();
  // tried == FIRMWARE_VERSION: this is the new image; it clears the key once it confirms.
  const bool rolledBack = tried.length() && tried != FIRMWARE_VERSION;
  if (!dl.length() && !rolledBack) {
    return;
  }
  if (dl.length()) {
    otaFail(dl.c_str(), "size", false);
  }
  if (rolledBack) {
    if (esp_ota_get_last_invalid_partition()) {
      otaFail(tried.c_str(), "boot", false);
    } else {
      LOG("ota %s: another image runs now, not a rollback\n", tried.c_str());
    }
  }
  if (p.begin("ota", false)) {
    if (dl.length()) {
      p.remove("dl");
    }
    if (rolledBack) {
      p.remove("try");
    }
    p.end();
  }
}

// After the first 200/204 poll: this image works, keep it.
inline void otaConfirm() {
  if (gOtaConfirmed) {
    return;
  }
  gOtaConfirmed = true;
  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK &&
      state == ESP_OTA_IMG_PENDING_VERIFY) {
    const esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    LOG("ota: %s confirmed (%d)\n", FIRMWARE_VERSION, static_cast<int>(err));
  }
  Preferences p;
  if (p.begin("ota", true)) {
    const bool pending = p.isKey("try");
    p.end();
    if (pending && p.begin("ota", false)) {
      p.remove("try");
      p.end();
    }
  }
}

// Query suffix for the poll: firmware always, ota_error once.
inline void otaAppendQuery(String &path) {
  path += "&firmware=";
  path += FIRMWARE_VERSION;
  if (gOtaErrorPending[0]) {
    path += "&ota_error=";
    path += gOtaErrorPending;
  }
}

// The console answered 200/204: it has the version and the error.
inline void otaPollAccepted() {
  gOtaErrorPending[0] = 0;
  otaConfirm();
}

inline bool otaLooksLikeVersion(const char *v) {
  int dots = 0;
  if (!v[0]) {
    return false;
  }
  for (const char *p = v; *p; p++) {
    if (*p == '.') {
      dots++;
    } else if (*p < '0' || *p > '9') {
      return false;
    }
  }
  return dots == 2;
}

inline bool otaLooksLikeSha(const char *s) {
  if (strlen(s) != 64) {
    return false;
  }
  for (const char *p = s; *p; p++) {
    if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f'))) {
      return false;
    }
  }
  return true;
}

// Any 200 body, whatever its rev. Absent or malformed `ota`: no offer.
inline void otaParseOffer(const char *body) {
  gOtaOfferValid = false;
  const char *p = body ? strstr(body, "\"ota\":") : nullptr;
  if (!p) {
    return;
  }
  p += 6;
  while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') {
    p++;
  }
  // No nested objects and no braces in its strings: the block ends at the first '}'.
  const char *end = (*p == '{') ? strchr(p, '}') : nullptr;
  if (!end || end - p > 400) {
    LOGLN("ota: offer not an object");
    return;
  }
  char block[402];
  const size_t n = static_cast<size_t>(end - p + 1);
  memcpy(block, p, n);
  block[n] = 0;
  OtaOffer o;
  memset(&o, 0, sizeof(o));
  const int size = jsonGetInt(block, "size", -1);
  if (!jsonGetString(block, "version", o.version, sizeof(o.version)) ||
      !jsonGetString(block, "url", o.url, sizeof(o.url)) ||
      !jsonGetString(block, "sha256", o.sha256, sizeof(o.sha256)) || size <= 0 ||
      !otaLooksLikeVersion(o.version) || o.url[0] != '/' || !otaLooksLikeSha(o.sha256)) {
    LOGLN("ota: offer malformed, ignored");
    return;
  }
  o.size = static_cast<uint32_t>(size);
  gOtaOffer = o;
  gOtaOfferValid = true;
}

inline bool otaShouldStart(const OtaOffer &o) {
  if (strcmp(o.version, FIRMWARE_VERSION) == 0) {
    return false;
  }
  if (strcmp(o.version, gOtaBlockedVersion) == 0) {
    LOG("ota %s: not retried until reboot\n", o.version);
    return false;
  }
  if (strcmp(o.version, gOtaFailedVersion) == 0 && millis() - gOtaFailedMs < kOtaRetryMs) {
    LOG("ota %s: retry after an hour\n", o.version);
    return false;
  }
  return true;
}

// Asks the loop for the Updating screen. true: the loop took it (phase DOWNLOADING), the screen
// is the task's to drive until RESTARTING or FAILED.
inline bool otaAskLoop(const char *version) {
  snprintf(gOtaUiVersion, sizeof(gOtaUiVersion), "%s", version);
  gOtaUiPct = 0;
  gOtaUiPhase = OTA_UI_ASK;
  const unsigned long t0 = millis();
  for (;;) {
    const uint8_t ph = gOtaUiPhase;
    if (ph == OTA_UI_DOWNLOADING) {
      return true;
    }
    if (ph == OTA_UI_DECLINED) {
      gOtaUiPhase = OTA_UI_NONE;
      return false;
    }
    if (millis() - t0 >= kOtaAskWaitMs) {
      // The loop is busy elsewhere (pairing waits run there). Withdraw unless it just answered.
      uint8_t ask = OTA_UI_ASK;
      if (gOtaUiPhase.compare_exchange_strong(ask, OTA_UI_NONE)) {
        LOGLN("ota: screen did not answer, next poll");
        return false;
      }
      continue;
    }
    delay(10);
  }
}

inline void otaHex(const uint8_t *d, char *out) {
  for (int i = 0; i < 32; i++) {
    snprintf(out + i * 2, 3, "%02x", d[i]);
  }
}

struct OtaHeapLow {
  size_t freeInt;
  size_t blockInt;
  size_t freePsram;
};

inline void otaHeapSample(OtaHeapLow *low, const char *when) {
  const size_t freeInt = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const size_t blockInt = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const size_t freePsram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  if (freeInt < low->freeInt) {
    low->freeInt = freeInt;
  }
  if (blockInt < low->blockInt) {
    low->blockInt = blockInt;
  }
  if (freePsram < low->freePsram) {
    low->freePsram = freePsram;
  }
  if (when) {
    LOG("ota heap %s: internal free %u largest %u, psram free %u\n", when, static_cast<unsigned>(freeInt),
        static_cast<unsigned>(blockInt), static_cast<unsigned>(freePsram));
  }
}

// Streams the image into the inactive slot. true: written, checked and bootable.
inline bool otaDownload(const OtaOffer &o) {
  const esp_partition_t *slot = esp_ota_get_next_update_partition(nullptr);
  if (!slot || o.size > slot->size) {
    otaFail(o.version, "size", true);
    return false;
  }
  OtaHeapLow low{SIZE_MAX, SIZE_MAX, SIZE_MAX};
  otaHeapSample(&low, "start");
  if (low.freeInt < kOtaMinFree || low.blockInt < kOtaMinBlock) {
    otaFail(o.version, "heap", false);
    return false;
  }
  const String url = consoleBaseUrl() + o.url;
  HTTPClient http;
  http.setTimeout(15000);
  NetworkClientSecure secure;
  NetworkClient plain;
  bool began = false;
  if (url.startsWith("https://")) {
    secure.useBuiltinCACertBundle();
    began = http.begin(secure, url);
  } else {
    began = http.begin(plain, url);
  }
  if (!began) {
    otaFail(o.version, "connect", false);
    return false;
  }
  const int code = http.GET();
  LOG("ota GET %s %d\n", o.url, code);
  if (code <= 0) {
    http.end();
    otaFail(o.version, "connect", false);
    return false;
  }
  if (code != HTTP_CODE_OK) {
    http.end();
    otaFail(o.version, "http", false);
    return false;
  }
  otaHeapSample(&low, "connected");
  const int len = http.getSize();
  if (len != static_cast<int>(o.size)) {
    LOG("ota length %d, offer %u\n", len, static_cast<unsigned>(o.size));
    http.end();
    otaFail(o.version, "size", true);
    return false;
  }
  uint8_t *buf = static_cast<uint8_t *>(malloc(4096));
  if (!buf) {
    http.end();
    otaFail(o.version, "heap", false);
    return false;
  }
  otaMarkDownload(o.version);
  if (!Update.begin(o.size, U_FLASH)) {
    otaMarkDownload(nullptr);
    LOG("ota Update.begin error %u\n", Update.getError());
    free(buf);
    http.end();
    otaFail(o.version, "write", false);
    return false;
  }
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts(&sha, 0);
  NetworkClient *s = http.getStreamPtr();
  size_t got = 0;
  bool writeFailed = false;
  unsigned long lastData = millis();
  const unsigned long t0 = lastData;
  unsigned long lastSample = t0;
  while (got < o.size && http.connected()) {
    const int avail = s->available();
    if (avail <= 0) {
      if (millis() - lastData > kOtaStallMs) {
        LOGLN("ota: download stalled");
        break;
      }
      delay(2);
      continue;
    }
    size_t want = static_cast<size_t>(avail);
    if (want > 4096) {
      want = 4096;
    }
    if (want > o.size - got) {
      want = o.size - got;
    }
    const int n = s->read(buf, want);
    if (n <= 0) {
      continue;
    }
    lastData = millis();
    mbedtls_sha256_update(&sha, buf, n);
    if (Update.write(buf, n) != static_cast<size_t>(n)) {
      LOG("ota Update.write error %u\n", Update.getError());
      writeFailed = true;
      break;
    }
    got += n;
    gOtaUiPct = static_cast<uint8_t>((static_cast<uint64_t>(got) * 100U) / o.size);
    if (lastData - lastSample >= 1000) {
      lastSample = lastData;
      otaHeapSample(&low, "download");
    } else {
      otaHeapSample(&low, nullptr);
    }
  }
  uint8_t digest[32];
  mbedtls_sha256_finish(&sha, digest);
  mbedtls_sha256_free(&sha);
  free(buf);
  http.end();
  otaMarkDownload(nullptr);
  LOG("ota %u of %u bytes in %lu ms\n", static_cast<unsigned>(got), static_cast<unsigned>(o.size), millis() - t0);
  LOG("ota heap lowest: internal free %u largest %u, psram free %u\n", static_cast<unsigned>(low.freeInt),
      static_cast<unsigned>(low.blockInt), static_cast<unsigned>(low.freePsram));
  if (writeFailed) {
    Update.abort();
    otaFail(o.version, "write", false);
    return false;
  }
  if (got != o.size) {
    // The stream ended early (Wi-Fi, server): reported as size, but the image is not known bad.
    Update.abort();
    otaFail(o.version, "size", false);
    return false;
  }
  char hex[65];
  otaHex(digest, hex);
  if (strcmp(hex, o.sha256) != 0) {
    LOG("ota sha256 %s, offer %s\n", hex, o.sha256);
    Update.abort();
    otaFail(o.version, "sha", true);
    return false;
  }
  if (!Update.end()) {
    LOG("ota Update.end error %u\n", Update.getError());
    otaFail(o.version, "write", false);
    return false;
  }
  return true;
}

// After the poll that carried the offer, its connection closed and its body handed to the loop.
inline void otaMaybeApply() {
  if (!gOtaOfferValid) {
    return;
  }
  gOtaOfferValid = false;
  const OtaOffer o = gOtaOffer;
  if (!otaShouldStart(o)) {
    return;
  }
  if (!otaAskLoop(o.version)) {
    return;
  }
  LOG("ota: %s -> %s (%u bytes)\n", FIRMWARE_VERSION, o.version, static_cast<unsigned>(o.size));
  if (!otaDownload(o)) {
    gOtaUiPhase = OTA_UI_FAILED;
    return;
  }
  Preferences p;
  if (p.begin("ota", false)) {
    p.putString("try", o.version);
    p.end();
  }
  LOG("ota: %s written, restarting\n", o.version);
  gOtaUiPct = 100;
  gOtaUiPhase = OTA_UI_RESTARTING;
  delay(600);  // the loop paints "Restarting"
  ESP.restart();
}

// ---------------------------------------------------------------- screen (loop)

inline void otaPaintRing(int from, int to) {
  const PageTheme *t = uiTheme();
  if (from < 0) {
    uiFillArcSpan(kLevelStartDeg, kLevelStartDeg + kLevelSpanDeg, t->ringTrack);
    from = 0;
  }
  if (to > from) {
    uiFillArcSpan(kLevelStartDeg + kLevelSpanDeg * from / 100.0f, kLevelStartDeg + kLevelSpanDeg * to / 100.0f,
                  t->accent);
  }
}

// Called by uiPaint for UI_UPDATING.
inline void otaPaintScreen() {
  if (!gDisplayOk || !gLcd) {
    return;
  }
  const PageTheme *t = uiTheme();
  const uint8_t ph = gOtaUiPhase;
  gOtaShownPhase = ph;
  if (ph == OTA_UI_FAILED) {
    gLcd->drawCircle(kScreenCx, kScreenCy, kRingOuter, t->error);
    displayTextCenter("Update failed", 120, 1, t->ink);
    gOtaShownPct = -1;
    return;
  }
  displayTextCenter(ph == OTA_UI_RESTARTING ? "Restarting" : "Updating", 108, 2, t->ink);
  displayTextCenter(gOtaUiVersion, 138, 1, t->mute);
  const int pct = ph == OTA_UI_RESTARTING ? 100 : gOtaUiPct.load();
  otaPaintRing(-1, pct);
  gOtaShownPct = pct;
}

// Leaves the Updating screen after a failure: back to Ready or Empty, asleep if it was.
inline void otaUiLeave(unsigned long now) {
  gOtaUiPhase = OTA_UI_NONE;
  gOtaShownPhase = OTA_UI_NONE;
  const bool asleep = gScreenIdle;
  uiSet(uiFromRecipes());
  if (asleep) {
    gUiPainted = gUi;  // the panel is already off; nothing to paint until a touch wakes it
  }
  gNeedHueState = true;
  // A finger left on the glass during the update does not start a gesture.
  gTouchIgnoreUntil = now + 300;
  LOG("ota: back to %s\n", asleep ? "sleep" : "the page");
}

// Each loop pass, before applyScreen.
inline void otaLoopTick(unsigned long now) {
  const uint8_t ph = gOtaUiPhase;
  if (ph == OTA_UI_ASK) {
    const bool screenOk = gUi == UI_READY || gUi == UI_EMPTY;
    const bool touchQuiet = !gTouchDown && !gIdleWakeHold && !gDimDragging && !gTapWaitDouble && !gUiPressed &&
                            (now - gIdleLastMs) >= kOtaQuietMs;
    const bool hueQuiet = hueJobUserIdle();
    uint8_t ask = OTA_UI_ASK;
    if (!screenOk || !touchQuiet || !hueQuiet) {
      LOG("ota: not now (screen %d, touch %d, hue %d), next poll\n", screenOk, touchQuiet, hueQuiet);
      gOtaUiPhase.compare_exchange_strong(ask, OTA_UI_DECLINED);
      return;
    }
    if (!gOtaUiPhase.compare_exchange_strong(ask, OTA_UI_DOWNLOADING)) {
      return;  // the task withdrew the ask
    }
    gOtaWasAsleep = gScreenIdle;
    gOtaShownPct = -1;
    gOtaShownPhase = OTA_UI_NONE;
    gRefreshAt = false;
    uiSet(UI_UPDATING);
    LOG("ota: update screen%s\n", gScreenIdle ? " (asleep)" : "");
    return;
  }
  if (gUi != UI_UPDATING) {
    return;
  }
  if (gScreenIdle) {
    // Asleep: a touch wakes the panel straight to this screen; nothing else reacts to it.
    if (ph != OTA_UI_FAILED && touchIrqPressed()) {
      displayWakePanel();
      gScreenIdle = false;
      gIdleWakeHold = false;
      uiPaint();
      LOG("display wake (update)\n");
    } else if (ph == OTA_UI_FAILED) {
      otaUiLeave(now);
    }
    return;
  }
  if (ph == OTA_UI_FAILED) {
    if (gOtaShownPhase != OTA_UI_FAILED) {
      gOtaFailUntilMs = now + kOtaFailShowMs;
      uiPaint();
    } else if ((long)(now - gOtaFailUntilMs) >= 0) {
      otaUiLeave(now);
    }
    return;
  }
  if (ph != gOtaShownPhase || gUiPainted != UI_UPDATING) {
    uiPaint();
    return;
  }
  const int pct = gOtaUiPct.load();
  if (pct > gOtaShownPct) {
    otaPaintRing(gOtaShownPct, pct);
    gOtaShownPct = pct;
  }
}
