#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include "hue.h"
#include "log.h"
#include "pages.h"
#include "recipes.h"

// Un slot last-wins. HTTP Clip v2 corre en esta tarea, no en loop()/touch.

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
  char action[16];
  char rtype[16];
  char rid[40];
  uint8_t sceneCount;
  RecipeScene scenes[kMaxScenes];
  bool haveTargetOn;
  bool targetOn;
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
  bool ok;
  bool haveOn;
  bool on;
  bool haveBri;
  int pct;
  bool haveScene;
  bool sceneHave;
  char sceneName[25];
};

inline portMUX_TYPE gHueJobMux = portMUX_INITIALIZER_UNLOCKED;
inline HueJob gHuePending;
inline bool gHuePendingSet = false;
inline uint32_t gHueJobGen = 0;
inline HueJobResult gHueResult;
inline bool gHueResultReady = false;
inline bool gHueWorkerBusy = false;
inline TaskHandle_t gHueTask = nullptr;

inline uint32_t hueJobLatestGen() {
  portENTER_CRITICAL(&gHueJobMux);
  const uint32_t g = gHueJobGen;
  portEXIT_CRITICAL(&gHueJobMux);
  return g;
}

inline bool hueJobBusy() {
  portENTER_CRITICAL(&gHueJobMux);
  const bool busy = gHuePendingSet || gHueWorkerBusy;
  portEXIT_CRITICAL(&gHueJobMux);
  return busy;
}

inline void hueJobClearPending() {
  portENTER_CRITICAL(&gHueJobMux);
  gHuePendingSet = false;
  if (++gHueJobGen == 0) {
    gHueJobGen = 1;
  }
  portEXIT_CRITICAL(&gHueJobMux);
}

inline bool hueJobPost(const HueJob &job, bool replaceUser) {
  portENTER_CRITICAL(&gHueJobMux);
  if (!replaceUser &&
      ((gHuePendingSet && gHuePending.kind != HUE_JOB_REFRESH) ||
       (gHueWorkerBusy && !gHuePendingSet))) {
    portEXIT_CRITICAL(&gHueJobMux);
    return false;
  }
  uint32_t g = ++gHueJobGen;
  if (g == 0) {
    g = gHueJobGen = 1;
  }
  gHuePending = job;
  gHuePending.gen = g;
  gHuePendingSet = true;
  portEXIT_CRITICAL(&gHueJobMux);
  if (gHueTask) {
    xTaskNotifyGive(gHueTask);
  }
  return true;
}

inline bool hueJobTakeResult(HueJobResult *out) {
  if (!out) {
    return false;
  }
  portENTER_CRITICAL(&gHueJobMux);
  if (!gHueResultReady) {
    portEXIT_CRITICAL(&gHueJobMux);
    return false;
  }
  *out = gHueResult;
  gHueResultReady = false;
  const bool stale = out->gen != gHueJobGen || gHuePendingSet;
  portEXIT_CRITICAL(&gHueJobMux);
  return !stale;
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
  uint8_t next = (cur >= 0) ? static_cast<uint8_t>((cur + 1) % r->sceneCount) : 0;
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

inline HueArmResult hueJobArmRecipe(const char *pageId, const char *event, bool *nowOn) {
  const HueRecipe *r = recipesFind(pageId, event);
  if (!r) {
    LOG("%s %s: no recipe\n", pageId ? pageId : "?", event ? event : "?");
    return HUE_ARM_NONE;
  }
  if (WiFi.status() != WL_CONNECTED) {
    LOG("%s %s skipped: WiFi down\n", pageId, event);
    return HUE_ARM_ERR;
  }

  HueJob job{};
  job.kind = HUE_JOB_RECIPE;
  hueJobFillPage(&job, pageId);
  recipeCopyField(job.action, sizeof(job.action), r->action);
  recipeCopyField(job.rtype, sizeof(job.rtype), r->rtype);
  recipeCopyField(job.rid, sizeof(job.rid), r->rid);
  job.sceneCount = r->sceneCount;
  memcpy(job.scenes, r->scenes, sizeof(job.scenes));

  if (strcmp(r->action, "recall_scene") == 0) {
    uint8_t idx = 0;
    if (!hueJobPickNextScene(r, &idx)) {
      return HUE_ARM_ERR;
    }
    recipeCopyField(job.rid, sizeof(job.rid), r->scenes[idx].rid);
    pagesSetLastSceneRid(r->scenes[idx].rid);
    if (nowOn) {
      *nowOn = true;
    }
  } else if (strcmp(r->action, "off") == 0) {
    job.haveTargetOn = true;
    job.targetOn = false;
    pagesSetLastSceneRid("");
    if (nowOn) {
      *nowOn = false;
    }
  } else if (strcmp(r->action, "on") == 0) {
    job.haveTargetOn = true;
    job.targetOn = true;
    if (nowOn) {
      *nowOn = true;
    }
  } else if (strcmp(r->action, "toggle") == 0) {
    const bool nextOn = nowOn ? !(*nowOn) : true;
    job.haveTargetOn = true;
    job.targetOn = nextOn;
    if (nowOn) {
      *nowOn = nextOn;
    }
    if (!nextOn) {
      pagesSetLastSceneRid("");
    }
  } else {
    return HUE_ARM_ERR;
  }

  if (!hueJobPost(job, true)) {
    return HUE_ARM_ERR;
  }
  return HUE_ARM_OK;
}

inline bool hueJobArmDim(const char *pageId, int pct) {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }
  HueJob job{};
  job.kind = HUE_JOB_DIM;
  job.dimPct = pct;
  hueJobFillPage(&job, pageId);
  return hueJobPost(job, true);
}

inline bool hueJobArmRefresh(const char *pageId) {
  HueJob job{};
  job.kind = HUE_JOB_REFRESH;
  hueJobFillPage(&job, pageId);
  const HueRecipe *sc = recipesFindScene(pageId);
  if (sc) {
    job.sceneCount = sc->sceneCount;
    memcpy(job.scenes, sc->scenes, sizeof(job.scenes));
  }
  const HueRecipe *sh = recipesFind(pageId, "short");
  if (sh && sh->rid[0] &&
      (strcmp(sh->rtype, "light") == 0 || strcmp(sh->rtype, "grouped_light") == 0)) {
    recipeCopyField(job.rtype, sizeof(job.rtype), sh->rtype);
    recipeCopyField(job.rid, sizeof(job.rid), sh->rid);
  }
  return hueJobPost(job, false);
}

inline bool hueJobRunDim(const HueJob &job) {
  const int pct = job.dimPct < 1 ? 1 : (job.dimPct > 100 ? 100 : job.dimPct);
  if (job.dimMode == PAGE_DIM_GROUP && job.dimGroupRid[0]) {
    return huePutDimming("grouped_light", job.dimGroupRid, pct, false);
  }
  if (job.dimMode != PAGE_DIM_LIGHTS || job.dimLightCount == 0) {
    return false;
  }
  bool on[kMaxDimLights];
  bool got[kMaxDimLights];
  bool anyOn = false;
  for (uint8_t i = 0; i < kMaxDimLights; i++) {
    on[i] = false;
    got[i] = false;
  }
  bool ok = false;
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
    if (lightOn) {
      anyOn = true;
    }
  }
  for (uint8_t i = 0; i < job.dimLightCount && i < kMaxDimLights; i++) {
    if (!job.dimLights[i][0]) {
      continue;
    }
    if (anyOn) {
      if (!got[i] || !on[i]) {
        continue;
      }
      if (huePutDimming("light", job.dimLights[i], pct, false)) {
        ok = true;
      }
    } else if (huePutDimming("light", job.dimLights[i], pct, true)) {
      ok = true;
    }
  }
  return ok;
}

inline bool hueJobRunRecipe(const HueJob &job) {
  if (strcmp(job.action, "recall_scene") == 0) {
    return hueRecallScene(job.rid);
  }
  if (job.haveTargetOn) {
    return hueSetOn(job.rtype, job.rid, job.targetOn);
  }
  return hueExecute(job.action, job.rtype, job.rid, nullptr);
}

inline HueJobResult hueJobRunRefresh(const HueJob &job) {
  HueJobResult out{};
  out.kind = HUE_JOB_REFRESH;
  recipeCopyField(out.pageId, sizeof(out.pageId), job.pageId);
  out.ok = true;

  const char *rtype = nullptr;
  const char *rid = nullptr;
  const bool lightsDim = job.dimMode == PAGE_DIM_LIGHTS && job.dimLightCount > 0;
  if (job.dimMode == PAGE_DIM_GROUP && job.dimGroupRid[0]) {
    rtype = "grouped_light";
    rid = job.dimGroupRid;
  } else if (lightsDim) {
    rid = job.dimLights[0];
  } else if (job.rid[0] &&
             (strcmp(job.rtype, "light") == 0 || strcmp(job.rtype, "grouped_light") == 0)) {
    rtype = job.rtype;
    rid = job.rid;
  }

  if (rid && rid[0]) {
    bool on = false;
    int pct = 50;
    bool got = false;
    if (lightsDim) {
      bool anyOn = false;
      int firstOnPct = 0;
      int fallbackPct = pct;
      for (uint8_t i = 0; i < job.dimLightCount && i < kMaxDimLights; i++) {
        if (!job.dimLights[i][0]) {
          continue;
        }
        bool lightOn = false;
        int bri = 0;
        if (!hueGetLightState("light", job.dimLights[i], &lightOn, &bri)) {
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
      if (got) {
        on = anyOn;
        pct = anyOn ? firstOnPct : fallbackPct;
        out.haveOn = true;
        out.on = on;
        out.haveBri = true;
        out.pct = pct < 1 ? 1 : (pct > 100 ? 100 : pct);
      }
    } else if (rtype) {
      if (hueGetLightState(rtype, rid, &on, &pct)) {
        out.haveOn = true;
        out.on = on;
        out.haveBri = true;
        out.pct = pct < 1 ? 1 : (pct > 100 ? 100 : pct);
      }
    }
  }

  if (out.haveOn && !out.on) {
    out.haveScene = true;
    out.sceneHave = false;
    out.sceneName[0] = 0;
    return out;
  }
  if (job.sceneCount == 0) {
    return out;
  }
  int idx = -1;
  for (uint8_t i = 0; i < job.sceneCount; i++) {
    if (!job.scenes[i].rid[0]) {
      continue;
    }
    bool act = false;
    if (hueSceneActive(job.scenes[i].rid, &act) && act) {
      idx = static_cast<int>(i);
      break;
    }
  }
  out.haveScene = true;
  out.sceneHave = false;
  out.sceneName[0] = 0;
  if (idx >= 0) {
    recipeCopyField(out.sceneName, sizeof(out.sceneName), job.scenes[idx].name);
    out.sceneHave = out.sceneName[0] != 0;
  }
  return out;
}

inline bool hueJobRun(const HueJob &job, HueJobResult *out) {
  HueJobResult r{};
  r.kind = job.kind;
  r.gen = job.gen;
  recipeCopyField(r.pageId, sizeof(r.pageId), job.pageId);
  if (job.kind == HUE_JOB_REFRESH) {
    r = hueJobRunRefresh(job);
    r.gen = job.gen;
    recipeCopyField(r.pageId, sizeof(r.pageId), job.pageId);
    r.kind = HUE_JOB_REFRESH;
    *out = r;
    return r.ok;
  }
  if (job.kind == HUE_JOB_DIM) {
    r.ok = hueJobRunDim(job);
    *out = r;
    return r.ok;
  }
  if (job.kind == HUE_JOB_RECIPE) {
    r.ok = hueJobRunRecipe(job);
    *out = r;
    return r.ok;
  }
  r.ok = false;
  *out = r;
  return false;
}

inline void hueJobTask(void * /*arg*/) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    for (;;) {
      HueJob job{};
      portENTER_CRITICAL(&gHueJobMux);
      if (!gHuePendingSet) {
        gHueWorkerBusy = false;
        portEXIT_CRITICAL(&gHueJobMux);
        break;
      }
      job = gHuePending;
      gHuePendingSet = false;
      gHueWorkerBusy = true;
      portEXIT_CRITICAL(&gHueJobMux);

      HueJobResult result{};
      hueJobRun(job, &result);
      result.gen = job.gen;
      result.kind = job.kind;
      recipeCopyField(result.pageId, sizeof(result.pageId), job.pageId);

      portENTER_CRITICAL(&gHueJobMux);
      const bool stale = job.gen != gHueJobGen || gHuePendingSet;
      if (!stale) {
        gHueResult = result;
        gHueResultReady = true;
      }
      if (!gHuePendingSet) {
        gHueWorkerBusy = false;
      }
      portEXIT_CRITICAL(&gHueJobMux);
    }
  }
}

inline void hueJobBegin() {
  if (gHueTask) {
    return;
  }
  xTaskCreatePinnedToCore(hueJobTask, "hueJob", 16384, nullptr, 1, &gHueTask, 0);
}
