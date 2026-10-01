#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <atomic>
#include "hue.h"
#include "log.h"
#include "pages.h"
#include "recipes.h"
#include "hue_discover.h"
#include "snapshot.h"

// The hueJob task owns the Bridge: the kept-alive connection, the link state machine
// (HueLink) and every Clip v2 request. Nothing else talks to the Bridge.
//
// Two slots: user (recipe / dimmer, last-wins) and background (refresh). User work runs
// first and preempts background work between requests. The register snapshot is
// background work too. Results go back to the loop through a queue; the loop owns the
// UI and decides what to paint.

enum HueJobKind : uint8_t {
  HUE_JOB_NONE = 0,
  HUE_JOB_RECIPE,
  HUE_JOB_DIM,
  HUE_JOB_REFRESH,
};

enum HueArmResult : uint8_t { HUE_ARM_NONE = 0, HUE_ARM_OK, HUE_ARM_ERR };

struct HueJob {
  HueJobKind kind;
  uint32_t gen;
  char pageId[16];
  char event[16];
  char action[16];
  char rtype[16];
  char rid[40];      // Recipe target. recall_scene: the scene this tap chose.
  char rid2[40];     // Refresh: the double-tap child light (split fill).
  char prevRid[40];  // recall_scene: the page's scene cursor before this tap.
  uint8_t sceneCount;
  RecipeScene scenes[kMaxScenes];
  bool haveTargetOn;
  bool targetOn;
  bool resolveToggle;  // Toggle with stale or unknown state: GET then PUT here.
  bool scanScenes;     // Refresh: find the active scene (cached rid first).
  char cachedSceneRid[40];
  int dimPct;
  PageDimMode dimMode;
  char dimGroupRid[40];
  uint8_t dimLightCount;
  char dimLights[kMaxDimLights][40];
};

struct HueJobResult {
  uint32_t gen;
  HueJobKind kind;
  char pageId[16];
  char event[16];
  bool ok;
  bool haveOn;
  bool on;
  bool haveTapOn;
  bool tapOn;
  bool haveDblOn;
  bool dblOn;
  bool haveBri;
  int pct;
  bool haveScene;
  bool sceneHave;
  char sceneName[49];
  char sceneRid[40];
  bool isScene;
  char chosenRid[40];
  char prevRid[40];
};

// What the loop may paint at the gesture, before the PUT.
struct HueArmInfo {
  bool haveOn;
  bool on;
  bool isScene;
  char sceneRid[40];
  bool resolving;
};

inline portMUX_TYPE gHueJobMux = portMUX_INITIALIZER_UNLOCKED;
inline HueJob gHueUser;
inline bool gHueUserSet = false;
inline HueJob gHueBg;
inline bool gHueBgSet = false;
// Bumped by the loop on every user command and page change. A result whose gen is not the
// current one is not painted.
inline std::atomic<uint32_t> gHueUserGen{1};
inline QueueHandle_t gHueResultQ = nullptr;
// A user command is running in the hueJob task. With one queued, it keeps an update from
// starting (ota.h).
inline std::atomic<bool> gHueUserRunning{false};
inline TaskHandle_t gHueTask = nullptr;

// Link → loop: READY was reached through setup or pairing. The loop binds the bridge id
// (pages / recipes) and syncs the console. Paired = a new key.
inline std::atomic<bool> gHueBindPending{false};
inline std::atomic<bool> gHueBindPaired{false};
// Loop → link: Wi-Fi came up again.
inline std::atomic<uint32_t> gHueWifiEpoch{0};

// Register snapshot, built here for the console task.
inline SemaphoreHandle_t gHueSnapDone = nullptr;
inline std::atomic<uint32_t> gHueSnapReqSeq{0};
inline std::atomic<uint32_t> gHueSnapDoneSeq{0};
inline bool gHueSnapOk = false;
inline String gHueSnapLights;
inline String gHueSnapRooms;
inline String gHueSnapScenes;

inline void hueJobNotify() {
  if (gHueTask) {
    xTaskNotifyGive(gHueTask);
  }
}

inline uint32_t hueJobGen() { return gHueUserGen; }

// ---------------------------------------------------------------- loop side

// Page change: drop whatever the old page queued; older results stop painting.
inline void hueJobClearPending() {
  portENTER_CRITICAL(&gHueJobMux);
  gHueUserSet = false;
  gHueBgSet = false;
  portEXIT_CRITICAL(&gHueJobMux);
  gHueUserGen++;
}

inline void hueJobPostUser(HueJob &job) {
  const uint32_t g = ++gHueUserGen;
  job.gen = g;
  portENTER_CRITICAL(&gHueJobMux);
  gHueUser = job;
  gHueUserSet = true;
  portEXIT_CRITICAL(&gHueJobMux);
  hueJobNotify();
}

inline void hueJobPostBg(HueJob &job) {
  job.gen = gHueUserGen;
  portENTER_CRITICAL(&gHueJobMux);
  gHueBg = job;
  gHueBgSet = true;
  portEXIT_CRITICAL(&gHueJobMux);
  hueJobNotify();
}

// Any task: no user command queued or running.
inline bool hueJobUserIdle() {
  portENTER_CRITICAL(&gHueJobMux);
  const bool queued = gHueUserSet;
  portEXIT_CRITICAL(&gHueJobMux);
  return !queued && !gHueUserRunning;
}

inline bool hueJobTakeResult(HueJobResult *out) {
  if (!gHueResultQ || !out) {
    return false;
  }
  return xQueueReceive(gHueResultQ, out, 0) == pdTRUE;
}

inline bool hueJobPickNextScene(const HueRecipe *r, uint8_t *outIdx) {
  if (!r || r->sceneCount == 0 || !outIdx) {
    return false;
  }
  const char *last = pagesLastSceneRid();
  int cur = -1;
  if (last && last[0]) {
    for (uint8_t i = 0; i < r->sceneCount; i++) {
      if (strcmp(r->scenes[i].rid, last) == 0) {
        cur = static_cast<int>(i);
        break;
      }
    }
  }
  const uint8_t next = (cur >= 0) ? static_cast<uint8_t>((cur + 1) % r->sceneCount) : 0;
  for (uint8_t n = 0; n < r->sceneCount; n++) {
    const uint8_t i = static_cast<uint8_t>((next + n) % r->sceneCount);
    if (r->scenes[i].rid[0]) {
      *outIdx = i;
      return true;
    }
  }
  return false;
}

inline void hueJobFillPage(HueJob *job, const char *pageId) {
  recipeCopyField(job->pageId, sizeof(job->pageId), pageId);
  const Page *p = pagesActive();
  if (!p) {
    return;
  }
  job->dimMode = p->dimMode;
  recipeCopyField(job->dimGroupRid, sizeof(job->dimGroupRid), p->dimGroupRid);
  job->dimLightCount = p->dimLightCount;
  for (uint8_t i = 0; i < kMaxDimLights; i++) {
    recipeCopyField(job->dimLights[i], sizeof(job->dimLights[0]), p->dimLights[i]);
  }
}

// fresh/curOn: what the loop knows about the target's on state, and whether it is recent
// enough to guess a toggle from. The scene cursor moves here, at the gesture.
inline HueArmResult hueJobArmRecipe(const char *pageId, const char *event, bool fresh, bool curOn,
                                    HueArmInfo *info) {
  HueArmInfo local{};
  if (!info) {
    info = &local;
  }
  *info = HueArmInfo{};
  const HueRecipe *r = recipesFind(pageId, event);
  if (!r) {
    LOG("%s %s: no recipe\n", pageId ? pageId : "?", event ? event : "?");
    return HUE_ARM_NONE;
  }
  if (WiFi.status() != WL_CONNECTED || !hueLinkUsable()) {
    LOG("%s %s skipped: no Bridge link\n", pageId, event);
    return HUE_ARM_ERR;
  }

  static HueJob job;  // Loop only. ~1 KB kept off the loop stack.
  job = HueJob{};
  job.kind = HUE_JOB_RECIPE;
  hueJobFillPage(&job, pageId);
  recipeCopyField(job.event, sizeof(job.event), event);
  recipeCopyField(job.action, sizeof(job.action), r->action);
  recipeCopyField(job.rtype, sizeof(job.rtype), r->rtype);
  recipeCopyField(job.rid, sizeof(job.rid), r->rid);
  job.sceneCount = r->sceneCount;
  memcpy(job.scenes, r->scenes, sizeof(job.scenes));
  const bool split = recipesTwoChildLights(pageId, nullptr, nullptr);

  if (strcmp(r->action, "recall_scene") == 0) {
    uint8_t idx = 0;
    if (!hueJobPickNextScene(r, &idx)) {
      return HUE_ARM_ERR;
    }
    recipeCopyField(job.prevRid, sizeof(job.prevRid), pagesLastSceneRid());
    recipeCopyField(job.rid, sizeof(job.rid), r->scenes[idx].rid);
    pagesSetLastSceneRid(r->scenes[idx].rid);
    info->haveOn = true;
    info->on = true;
    info->isScene = true;
    recipeCopyField(info->sceneRid, sizeof(info->sceneRid), r->scenes[idx].rid);
  } else if (strcmp(r->action, "off") == 0) {
    job.haveTargetOn = true;
    job.targetOn = false;
    if (!split) {
      pagesSetLastSceneRid("");
    }
    info->haveOn = true;
    info->on = false;
  } else if (strcmp(r->action, "on") == 0) {
    job.haveTargetOn = true;
    job.targetOn = true;
    info->haveOn = true;
    info->on = true;
  } else if (strcmp(r->action, "toggle") == 0) {
    if (fresh) {
      job.haveTargetOn = true;
      job.targetOn = !curOn;
      info->haveOn = true;
      info->on = !curOn;
      if (!job.targetOn && !split) {
        pagesSetLastSceneRid("");
      }
    } else {
      job.resolveToggle = true;
      info->resolving = true;
    }
  } else {
    return HUE_ARM_ERR;
  }

  hueJobPostUser(job);
  return HUE_ARM_OK;
}

inline bool hueJobArmDim(const char *pageId, int pct) {
  if (WiFi.status() != WL_CONNECTED || !hueLinkUsable()) {
    return false;
  }
  static HueJob job;  // Loop only.
  job = HueJob{};
  job.kind = HUE_JOB_DIM;
  job.dimPct = pct;
  hueJobFillPage(&job, pageId);
  hueJobPostUser(job);
  return true;
}

inline bool hueJobArmRefresh(const char *pageId, bool scanScenes) {
  if (!pageId || !pageId[0] || !hueLinkUsable()) {
    return false;
  }
  static HueJob job;  // Loop only.
  job = HueJob{};
  job.kind = HUE_JOB_REFRESH;
  job.scanScenes = scanScenes;
  hueJobFillPage(&job, pageId);
  recipeCopyField(job.cachedSceneRid, sizeof(job.cachedSceneRid), pagesLastSceneRid());
  const HueRecipe *sc = recipesFindScene(pageId);
  if (sc) {
    job.sceneCount = sc->sceneCount;
    memcpy(job.scenes, sc->scenes, sizeof(job.scenes));
  }
  const HueRecipe *tap = nullptr;
  const HueRecipe *dbl = nullptr;
  if (recipesTwoChildLights(pageId, &tap, &dbl)) {
    recipeCopyField(job.rtype, sizeof(job.rtype), "light");
    recipeCopyField(job.rid, sizeof(job.rid), tap->rid);
    recipeCopyField(job.rid2, sizeof(job.rid2), dbl->rid);
  } else {
    const HueRecipe *sh = recipesFind(pageId, "short");
    if (sh && sh->rid[0] && (strcmp(sh->rtype, "light") == 0 || strcmp(sh->rtype, "grouped_light") == 0)) {
      recipeCopyField(job.rtype, sizeof(job.rtype), sh->rtype);
      recipeCopyField(job.rid, sizeof(job.rid), sh->rid);
    }
  }
  hueJobPostBg(job);
  return true;
}

// Console task: ask the hueJob task for the register snapshot and wait for it.
inline bool hueJobSnapshot(String *lights, String *rooms, String *scenes, unsigned long timeoutMs) {
  if (!gHueTask || !gHueSnapDone || !lights || !rooms || !scenes) {
    return false;
  }
  xSemaphoreTake(gHueSnapDone, 0);
  const uint32_t seq = ++gHueSnapReqSeq;
  hueJobNotify();
  const unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    if (xSemaphoreTake(gHueSnapDone, pdMS_TO_TICKS(1000)) != pdTRUE) {
      continue;
    }
    if (gHueSnapDoneSeq != seq) {
      continue;
    }
    if (!gHueSnapOk) {
      return false;
    }
    *lights = gHueSnapLights;
    *rooms = gHueSnapRooms;
    *scenes = gHueSnapScenes;
    return true;
  }
  return false;
}

// ---------------------------------------------------------------- hueJob task side

inline void huePostResult(const HueJobResult &r) {
  if (!gHueResultQ) {
    return;
  }
  if (xQueueSend(gHueResultQ, &r, 0) != pdTRUE) {
    HueJobResult drop;
    xQueueReceive(gHueResultQ, &drop, 0);
    xQueueSend(gHueResultQ, &r, 0);
  }
}

// Taken from the queue and marked running in one step, so hueJobUserIdle never sees neither.
// hueServeUser clears the mark once it ran.
inline bool hueTakeUser(HueJob *out) {
  portENTER_CRITICAL(&gHueJobMux);
  const bool have = gHueUserSet;
  if (have) {
    *out = gHueUser;
    gHueUserSet = false;
    gHueUserRunning = true;
  }
  portEXIT_CRITICAL(&gHueJobMux);
  return have;
}

inline bool hueTakeBg(HueJob *out) {
  portENTER_CRITICAL(&gHueJobMux);
  const bool have = gHueBgSet;
  if (have) {
    *out = gHueBg;
    gHueBgSet = false;
  }
  portEXIT_CRITICAL(&gHueJobMux);
  return have;
}

inline int hueJobClampPct(int pct) {
  if (pct < 1) {
    return 1;
  }
  if (pct > 100) {
    return 100;
  }
  return pct;
}

inline bool hueJobRunDim(const HueJob &job) {
  const int pct = hueJobClampPct(job.dimPct);
  if (job.dimMode == PAGE_DIM_GROUP && job.dimGroupRid[0]) {
    return huePutDimming("grouped_light", job.dimGroupRid, pct, false);
  }
  if (job.dimMode != PAGE_DIM_LIGHTS || job.dimLightCount == 0) {
    return false;
  }
  bool on[kMaxDimLights] = {};
  bool got[kMaxDimLights] = {};
  bool anyOn = false;
  for (uint8_t i = 0; i < job.dimLightCount && i < kMaxDimLights; i++) {
    if (!job.dimLights[i][0]) {
      continue;
    }
    bool lightOn = false;
    if (!hueGetLightState("light", job.dimLights[i], &lightOn, nullptr)) {
      continue;
    }
    got[i] = true;
    on[i] = lightOn;
    anyOn = anyOn || lightOn;
  }
  bool ok = false;
  for (uint8_t i = 0; i < job.dimLightCount && i < kMaxDimLights; i++) {
    if (!job.dimLights[i][0]) {
      continue;
    }
    if (anyOn) {
      if (got[i] && on[i] && huePutDimming("light", job.dimLights[i], pct, false)) {
        ok = true;
      }
    } else if (huePutDimming("light", job.dimLights[i], pct, true)) {
      ok = true;
    }
  }
  return ok;
}

// No settle delay and no readback here: the loop queues a background refresh afterwards.
inline bool hueJobRunRecipe(const HueJob &job, HueJobResult *out) {
  if (strcmp(job.action, "recall_scene") == 0) {
    out->isScene = true;
    recipeCopyField(out->chosenRid, sizeof(out->chosenRid), job.rid);
    recipeCopyField(out->prevRid, sizeof(out->prevRid), job.prevRid);
    if (job.sceneCount == 0) {
      return false;
    }
    uint8_t start = 0;
    for (uint8_t i = 0; i < job.sceneCount; i++) {
      if (job.rid[0] && strcmp(job.scenes[i].rid, job.rid) == 0) {
        start = i;
        break;
      }
    }
    for (uint8_t n = 0; n < job.sceneCount; n++) {
      const uint8_t i = static_cast<uint8_t>((start + n) % job.sceneCount);
      if (!job.scenes[i].rid[0]) {
        continue;
      }
      const int code = hueRecallSceneHttp(job.scenes[i].rid);
      if (code == HTTP_CODE_NOT_FOUND) {
        LOG("Hue recall 404 skip %s\n", job.scenes[i].rid);
        continue;
      }
      if (code != HTTP_CODE_OK) {
        return false;
      }
      out->haveOn = true;
      out->on = true;
      out->haveScene = true;
      recipeCopyField(out->sceneRid, sizeof(out->sceneRid), job.scenes[i].rid);
      recipeCopyField(out->sceneName, sizeof(out->sceneName), job.scenes[i].name);
      out->sceneHave = out->sceneName[0] != 0;
      return true;
    }
    return false;
  }
  if (job.resolveToggle) {
    bool cur = false;
    if (!hueGetOn(job.rtype, job.rid, &cur)) {
      return false;
    }
    if (!hueSetOn(job.rtype, job.rid, !cur)) {
      return false;
    }
    out->haveOn = true;
    out->on = !cur;
    return true;
  }
  if (job.haveTargetOn) {
    if (!hueSetOn(job.rtype, job.rid, job.targetOn)) {
      return false;
    }
    out->haveOn = true;
    out->on = job.targetOn;
    return true;
  }
  return false;
}

inline void hueRunUserJob(const HueJob &job) {
  static HueJobResult r;  // hueJob task only.
  r = HueJobResult{};
  r.kind = job.kind;
  r.gen = job.gen;
  recipeCopyField(r.pageId, sizeof(r.pageId), job.pageId);
  recipeCopyField(r.event, sizeof(r.event), job.event);
  const unsigned long t0 = millis();
  if (job.kind == HUE_JOB_RECIPE) {
    r.ok = hueJobRunRecipe(job, &r);
  } else if (job.kind == HUE_JOB_DIM) {
    r.ok = hueJobRunDim(job);
  }
  LOG("job %s %s %s %lums\n", job.kind == HUE_JOB_DIM ? "dim" : "recipe", job.event, r.ok ? "ok" : "FAIL",
      millis() - t0);
  huePostResult(r);
}

// Runs every waiting user command. True if any ran (the caller's background work is stale).
inline bool hueServeUser() {
  HueJob job;
  bool ran = false;
  while (hueTakeUser(&job)) {
    hueRunUserJob(job);
    gHueUserRunning = false;
    ran = true;
  }
  return ran;
}

// Not usable any more (pairing, Wi-Fi down): commands that were queued fail, so the loop
// can undo its optimistic paint.
inline void hueFailUser() {
  HueJob job;
  while (hueTakeUser(&job)) {
    HueJobResult r{};
    r.kind = job.kind;
    r.gen = job.gen;
    recipeCopyField(r.pageId, sizeof(r.pageId), job.pageId);
    recipeCopyField(r.event, sizeof(r.event), job.event);
    if (strcmp(job.action, "recall_scene") == 0) {
      r.isScene = true;
      recipeCopyField(r.chosenRid, sizeof(r.chosenRid), job.rid);
      recipeCopyField(r.prevRid, sizeof(r.prevRid), job.prevRid);
    }
    r.ok = false;
    huePostResult(r);
    gHueUserRunning = false;
  }
}

// Background state read. Gives way to user commands before each request; returns false
// (no result posted) when it did.
inline bool hueJobRunRefresh(const HueJob &job) {
  static HueJobResult out;  // hueJob task only.
  out = HueJobResult{};
  out.kind = HUE_JOB_REFRESH;
  out.gen = job.gen;
  out.ok = true;
  recipeCopyField(out.pageId, sizeof(out.pageId), job.pageId);

  const bool lightsDim = job.dimMode == PAGE_DIM_LIGHTS && job.dimLightCount > 0;
  if (job.rid[0] && job.rid2[0] && strcmp(job.rtype, "light") == 0) {
    bool tapOn = false;
    bool dblOn = false;
    int tapBri = 50;
    int dblBri = 50;
    if (hueServeUser()) {
      return false;
    }
    const bool gotTap = hueGetLightState("light", job.rid, &tapOn, &tapBri);
    if (hueServeUser()) {
      return false;
    }
    const bool gotDbl = hueGetLightState("light", job.rid2, &dblOn, &dblBri);
    out.haveTapOn = gotTap;
    out.tapOn = tapOn;
    out.haveDblOn = gotDbl;
    out.dblOn = dblOn;
    if (gotTap || gotDbl) {
      out.haveOn = true;
      out.on = (gotTap && tapOn) || (gotDbl && dblOn);
      out.haveBri = true;
      if (gotTap && tapOn) {
        out.pct = hueJobClampPct(tapBri);
      } else if (gotDbl && dblOn) {
        out.pct = hueJobClampPct(dblBri);
      } else {
        out.pct = hueJobClampPct(gotTap ? tapBri : dblBri);
      }
    }
    huePostResult(out);
    return true;
  }

  if (job.dimMode == PAGE_DIM_GROUP && job.dimGroupRid[0]) {
    if (hueServeUser()) {
      return false;
    }
    bool on = false;
    int pct = 50;
    if (hueGetLightState("grouped_light", job.dimGroupRid, &on, &pct)) {
      out.haveOn = true;
      out.on = on;
      out.haveBri = true;
      out.pct = hueJobClampPct(pct);
    }
  } else if (lightsDim) {
    bool anyOn = false;
    bool got = false;
    int firstOnPct = 50;
    int fallbackPct = 50;
    for (uint8_t i = 0; i < job.dimLightCount && i < kMaxDimLights; i++) {
      if (!job.dimLights[i][0]) {
        continue;
      }
      if (hueServeUser()) {
        return false;
      }
      bool lightOn = false;
      int bri = 0;
      if (!hueGetLightState("light", job.dimLights[i], &lightOn, &bri)) {
        continue;
      }
      if (!got) {
        fallbackPct = bri;
      }
      got = true;
      if (lightOn && !anyOn) {
        anyOn = true;
        firstOnPct = bri;
      }
    }
    if (got) {
      out.haveOn = true;
      out.on = anyOn;
      out.haveBri = true;
      out.pct = hueJobClampPct(anyOn ? firstOnPct : fallbackPct);
    }
  } else if (job.rid[0] && (strcmp(job.rtype, "light") == 0 || strcmp(job.rtype, "grouped_light") == 0)) {
    if (hueServeUser()) {
      return false;
    }
    bool on = false;
    int pct = 50;
    if (hueGetLightState(job.rtype, job.rid, &on, &pct)) {
      out.haveOn = true;
      out.on = on;
      out.haveBri = true;
      out.pct = hueJobClampPct(pct);
    }
  }

  if (out.haveOn && !out.on) {
    out.haveScene = true;
    out.sceneHave = false;
    huePostResult(out);
    return true;
  }
  if (!job.scanScenes || job.sceneCount == 0) {
    huePostResult(out);
    return true;
  }

  // Cached scene first: usually the one on, so one GET.
  int first = -1;
  for (uint8_t i = 0; i < job.sceneCount; i++) {
    if (job.cachedSceneRid[0] && strcmp(job.scenes[i].rid, job.cachedSceneRid) == 0) {
      first = i;
      break;
    }
  }
  int found = -1;
  for (int n = -1; n < static_cast<int>(job.sceneCount) && found < 0; n++) {
    const int i = n < 0 ? first : n;
    if (i < 0 || (n >= 0 && n == first) || !job.scenes[i].rid[0]) {
      continue;
    }
    if (hueServeUser()) {
      return false;
    }
    bool act = false;
    if (hueSceneActive(job.scenes[i].rid, &act) && act) {
      found = i;
    }
  }
  out.haveScene = true;
  out.sceneHave = false;
  if (found >= 0) {
    recipeCopyField(out.sceneName, sizeof(out.sceneName), job.scenes[found].name);
    recipeCopyField(out.sceneRid, sizeof(out.sceneRid), job.scenes[found].rid);
    out.sceneHave = out.sceneName[0] != 0;
  }
  huePostResult(out);
  return true;
}

// ---------------------------------------------------------------- link state machine

static const unsigned long kLinkBackoffMs[] = {2000, 5000, 10000, 30000, 60000};
static const unsigned long kProbeBackoffMs[] = {3000, 5000, 10000, 30000, 60000};
static const unsigned long kPairFastMs = 90000;
static const unsigned long kOldKeyProbeMs = 30000;

inline unsigned long gLinkNextMs = 0;
inline uint8_t gLinkFails = 0;
inline unsigned long gPairStartMs = 0;
inline bool gPairFind = false;
inline unsigned long gOldKeyProbeAt = 0;
inline uint32_t gLinkClrSeen = 1;
inline uint32_t gLinkWifiSeen = 0;

inline unsigned long hueBackoff(const unsigned long *table, uint8_t fails) {
  const uint8_t i = fails == 0 ? 0 : (fails > 5 ? 4 : fails - 1);
  return table[i];
}

inline void hueLinkSet(HueLink l) {
  if (gHueLink != l) {
    LOG("link %u -> %u\n", (unsigned)gHueLink.load(), (unsigned)l);
    gHueLink = l;
  }
}

inline bool hueLinkCleared() { return gHuePairCancel || gHueClrEpoch != gLinkClrSeen; }

inline void hueLinkReady(bool paired) {
  if (hueLinkCleared()) {
    return;
  }
  hueSaveStore();
  gLinkFails = 0;
  gHueRejectCount = 0;
  gHueKeyRejected = false;
  hueLinkSet(LINK_READY);
  if (paired) {
    gHueBindPaired = true;
  }
  gHueBindPending = true;
}

inline void hueLinkStartPairing() {
  gPairStartMs = millis();
  gOldKeyProbeAt = millis() + kOldKeyProbeMs;
  gLinkNextMs = millis();
  hueLinkSet(LINK_PAIRING);
}

inline void hueLinkFail() {
  if (gLinkFails < 255) {
    gLinkFails++;
  }
  gLinkNextMs = millis() + hueBackoff(kLinkBackoffMs, gLinkFails);
  // First miss keeps "Loading"; from the second one the screen says "No Bridge".
  if (gLinkFails >= 2 || gHueLink != LINK_START) {
    hueLinkSet(LINK_SEARCHING);
  }
}

// Handles a keyed check's code during setup. True when the link was decided.
inline bool hueLinkOnKeyCode(int code) {
  if (code == HTTP_CODE_OK) {
    hueStrLock();
    const bool haveId = gHueBridgeId.length() > 0;
    const String ip = gHueBridgeIp;
    hueStrUnlock();
    if (!haveId) {
      String id;
      if (hueProbeBridge(ip, &id)) {
        hueSetBridgeId(id);
      }
    }
    hueLinkReady(false);
    return true;
  }
  if (code == HTTP_CODE_UNAUTHORIZED || code == HTTP_CODE_FORBIDDEN) {
    if (gHueRejectCount >= 2) {
      LOGLN("Saved key rejected twice - pairing");
      gHueKeyRejected = true;
      hueLinkStartPairing();
    } else {
      gLinkNextMs = millis() + 1000;
    }
    return true;
  }
  return false;
}

// START / SEARCHING: reach the Bridge. Never pairs because of a timeout.
inline void hueLinkSetupStep() {
  hueStrLock();
  const bool haveKey = hueLooksLikeKey(gHueAppKey);
  const bool haveIp = hueLooksLikeIp(gHueBridgeIp);
  hueStrUnlock();

  if (haveKey && haveIp && hueLinkOnKeyCode(hueKeyCheck())) {
    return;
  }
  if (hueLinkCleared()) {
    return;
  }
  // The cached IP did not answer a keyed GET (or there is no key): look for the Bridge.
  if (!hueFindBridge(haveKey && haveIp)) {
    hueLinkFail();
    return;
  }
  if (!haveKey) {
    LOGLN("No Hue key - pairing");
    hueLinkStartPairing();
    return;
  }
  if (!hueLinkOnKeyCode(hueKeyCheck())) {
    hueLinkFail();
  }
}

// PAIRING: one POST per step. With a rejected saved key, the old key is re-tried too.
inline void hueLinkPairStep() {
  const unsigned long now = millis();
  if (gPairFind) {
    gPairFind = false;
    if (!hueFindBridge()) {
      hueLinkFail();
      return;
    }
  }
  if (gHueKeyRejected && (long)(now - gOldKeyProbeAt) >= 0) {
    gOldKeyProbeAt = now + kOldKeyProbeMs;
    if (hueKeyCheck() == HTTP_CODE_OK) {
      LOGLN("Saved key works again");
      hueLinkReady(false);
      return;
    }
  }
  hueStrLock();
  const bool haveIp = hueLooksLikeIp(gHueBridgeIp);
  hueStrUnlock();
  if (!haveIp) {
    gPairFind = true;
    gLinkNextMs = now + 2000;
    return;
  }
  if (huePairStep()) {
    hueKeyCheck();
    hueLinkReady(true);
    return;
  }
  gLinkNextMs = millis() + ((millis() - gPairStartMs) < kPairFastMs ? 500 : 3000);
}

// UNREACHABLE: keyed probe with backoff. After three misses, look for a new IP too.
inline void hueLinkProbeStep() {
  if (!hueRamReady()) {
    // Key or IP gone (HUECLR, HUEPAIR): only setup can recover.
    gLinkFails = 0;
    gLinkNextMs = millis();
    hueLinkSet(LINK_START);
    return;
  }
  if (hueKeyCheck() == HTTP_CODE_OK || gHueLink != LINK_UNREACHABLE) {
    gLinkFails = 0;
    return;
  }
  if (gLinkFails < 255) {
    gLinkFails++;
  }
  if (gLinkFails >= 3 && hueFindBridge(true)) {
    if (hueKeyCheck() == HTTP_CODE_OK) {
      hueSaveStore();
      gLinkFails = 0;
      return;
    }
  }
  gLinkNextMs = millis() + hueBackoff(kProbeBackoffMs, gLinkFails);
}

// HUEPAIR / BOOT 3 s (loop). The saved key is cleared here; the task finds the Bridge
// again and pairs.
inline bool huePairRequest() {
  if (!gHueTask) {
    return false;
  }
  hueClearSavedKey();
  gHuePairCancel = false;
  gHuePairEpoch = gHueClrEpoch.load();
  gHuePairReq = true;
  hueJobNotify();
  return true;
}

inline void hueLinkCheckEvents() {
  const uint32_t clr = gHueClrEpoch;
  if (clr != gLinkClrSeen) {
    // HUECLR: everything was forgotten. Start over (the loop shows No Wi-Fi meanwhile).
    gLinkClrSeen = clr;
    hueConnClose();
    gHuePairCancel = false;
    gHueKeyRejected = false;
    gHueRejectCount = 0;
    gLinkFails = 0;
    gPairFind = false;
    gLinkNextMs = millis();
    hueLinkSet(LINK_START);
  }
  if (gHuePairReq) {
    gHuePairReq = false;
    if (gHuePairEpoch == gHueClrEpoch) {
      LOGLN("Re-pair requested");
      hueConnClose();
      gHueKeyRejected = false;
      gHueRejectCount = 0;
      gLinkFails = 0;
      gPairFind = true;
      hueLinkStartPairing();
    }
  }
  const uint32_t wifi = gHueWifiEpoch;
  if (wifi != gLinkWifiSeen) {
    gLinkWifiSeen = wifi;
    hueConnClose();
    gLinkFails = 0;
    gLinkNextMs = millis();
    if (hueLinkUsable()) {
      // Probe right away: back to READY without discovery or a new register.
      hueLinkSet(LINK_UNREACHABLE);
    }
  }
}

inline void hueSnapServe() {
  const uint32_t want = gHueSnapReqSeq;
  if (want == gHueSnapDoneSeq) {
    return;
  }
  gHueSnapOk = hueLinkUsable() && hueBuildSnapshot(&gHueSnapLights, &gHueSnapRooms, &gHueSnapScenes);
  if (!gHueSnapOk) {
    gHueSnapLights = "";
    gHueSnapRooms = "";
    gHueSnapScenes = "";
  }
  gHueSnapDoneSeq = want;
  xSemaphoreGive(gHueSnapDone);
}

inline void hueSnapFailPending() {
  const uint32_t want = gHueSnapReqSeq;
  if (want == gHueSnapDoneSeq) {
    return;
  }
  gHueSnapOk = false;
  gHueSnapDoneSeq = want;
  xSemaphoreGive(gHueSnapDone);
}

inline bool hueLinkDue() { return (long)(millis() - gLinkNextMs) >= 0; }

inline void hueWorkerPass() {
  for (uint8_t guard = 0; guard < 64; guard++) {
    hueLinkCheckEvents();
    if (gWifiStaForgotten || WiFi.status() != WL_CONNECTED) {
      hueConnClose();
      hueFailUser();
      hueSnapFailPending();
      return;
    }
    if (!hueLinkUsable()) {
      hueFailUser();
      hueSnapFailPending();
      if (!hueLinkDue()) {
        return;
      }
      if (gHueLink == LINK_PAIRING) {
        hueLinkPairStep();
      } else {
        hueLinkSetupStep();
      }
      if (!hueLinkUsable()) {
        return;
      }
      continue;
    }
    if (hueServeUser()) {
      continue;
    }
    if (gHueLink == LINK_UNREACHABLE && hueLinkDue()) {
      hueLinkProbeStep();
      continue;
    }
    HueJob bg;
    if (hueTakeBg(&bg)) {
      const unsigned long t0 = millis();
      const bool done = hueJobRunRefresh(bg);
      LOG("job refresh %s %lums\n", done ? "ok" : "preempted", millis() - t0);
      continue;
    }
    if (gHueSnapReqSeq != gHueSnapDoneSeq) {
      hueSnapServe();
      continue;
    }
    hueConnIdleCheck();
    return;
  }
}

inline uint32_t hueWorkerWaitMs() {
  // Wi-Fi down: nothing to do until the loop notifies the Wi-Fi up edge.
  if (gWifiStaForgotten || WiFi.status() != WL_CONNECTED) {
    return 1000;
  }
  if (!hueLinkUsable() || gHueLink == LINK_UNREACHABLE) {
    const long left = (long)(gLinkNextMs - millis());
    if (left <= 0) {
      return 10;
    }
    return left > 1000 ? 1000 : static_cast<uint32_t>(left);
  }
  return 5000;
}

inline void hueJobTask(void * /*arg*/) {
  // Snapshot streams give way to taps between resources.
  gHueSnapYield = []() { hueServeUser(); };
  for (;;) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(hueWorkerWaitMs()));
    hueWorkerPass();
  }
}

inline void hueJobBegin() {
  if (gHueTask) {
    return;
  }
  hueStrEnsure();
  gHueResultQ = xQueueCreate(8, sizeof(HueJobResult));
  gHueSnapDone = xSemaphoreCreateBinary();
  gLinkClrSeen = gHueClrEpoch;
  // TLS + mDNS + snapshot streams: more stack than a recipe PUT.
  xTaskCreatePinnedToCore(hueJobTask, "hueJob", 24576, nullptr, 1, &gHueTask, 0);
}
