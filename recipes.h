#pragma once

#include <Preferences.h>
#include <string.h>
#include "json_util.h"
#include "pages.h"

// Recipes in NVS (a different key from pages). The finger only reads this, never Vercel.

static const uint8_t kMaxRecipes = 16;
static const uint8_t kMaxScenes = 8;
static const size_t kNvsStrMax = 3900;

struct RecipeScene {
  char rid[40];
  char name[25];
};

struct HueRecipe {
  char pageId[16];
  char event[16];
  char action[16];
  char rtype[16];
  char rid[40];
  uint8_t sceneCount;
  RecipeScene scenes[kMaxScenes];
};

inline HueRecipe gRecipes[kMaxRecipes];
inline uint8_t gRecipeCount = 0;
inline uint32_t gRecipeRev = 0;
inline String gRecipeBridgeId;
inline bool gNeedConsoleSync = false;
inline bool gRecipesBidReset = false;

inline bool recipeEventOk(const char *e) {
  return e && (strcmp(e, "short") == 0 || strcmp(e, "double_click") == 0);
}

inline bool recipeActionOk(const char *a) {
  return a && (strcmp(a, "on") == 0 || strcmp(a, "off") == 0 || strcmp(a, "recall_scene") == 0 ||
               strcmp(a, "toggle") == 0);
}

inline bool recipeRtypeOk(const char *r) {
  return r && (strcmp(r, "light") == 0 || strcmp(r, "grouped_light") == 0 || strcmp(r, "scene") == 0);
}

inline void recipeCopyField(char *dst, size_t n, const char *src) {
  if (!dst || n == 0) {
    return;
  }
  if (!src) {
    dst[0] = 0;
    return;
  }
  strncpy(dst, src, n - 1);
  dst[n - 1] = 0;
}

inline void recipeAppendOneJson(String &s, const HueRecipe &r) {
  s += "{\"pageId\":";
  jsonAppendEscaped(s, r.pageId);
  s += ",\"event\":";
  jsonAppendEscaped(s, r.event);
  s += ",\"action\":";
  jsonAppendEscaped(s, r.action);
  if (strcmp(r.action, "recall_scene") == 0 && r.sceneCount > 0) {
    s += ",\"targets\":[";
    for (uint8_t t = 0; t < r.sceneCount; t++) {
      if (t) {
        s += ',';
      }
      s += "{\"rtype\":\"scene\",\"rid\":";
      jsonAppendEscaped(s, r.scenes[t].rid);
      s += ",\"name\":";
      jsonAppendEscaped(s, r.scenes[t].name);
      s += '}';
    }
    s += ']';
  } else {
    s += ",\"target\":{\"rtype\":";
    jsonAppendEscaped(s, r.rtype);
    s += ",\"rid\":";
    jsonAppendEscaped(s, r.rid);
    s += '}';
  }
  s += '}';
}

inline String recipesToJsonRange(uint8_t from, uint8_t to) {
  String s = "[";
  bool first = true;
  for (uint8_t i = from; i < to && i < gRecipeCount; i++) {
    if (!first) {
      s += ',';
    }
    first = false;
    recipeAppendOneJson(s, gRecipes[i]);
  }
  s += "]";
  return s;
}

inline String recipesToJson() { return recipesToJsonRange(0, gRecipeCount); }

inline void recipeParseSceneTarget(const char *obj, void *ctx) {
  HueRecipe *r = static_cast<HueRecipe *>(ctx);
  if (!r || r->sceneCount >= kMaxScenes) {
    return;
  }
  char rtype[16];
  char rid[40];
  char name[48];
  rtype[0] = 0;
  rid[0] = 0;
  name[0] = 0;
  jsonGetString(obj, "rtype", rtype, sizeof(rtype));
  jsonGetString(obj, "rid", rid, sizeof(rid));
  jsonGetString(obj, "name", name, sizeof(name));
  if (!rid[0]) {
    return;
  }
  if (rtype[0] && strcmp(rtype, "scene") != 0) {
    return;
  }
  RecipeScene &sc = r->scenes[r->sceneCount];
  memset(&sc, 0, sizeof(sc));
  recipeCopyField(sc.rid, sizeof(sc.rid), rid);
  asciiFoldClip(sc.name, sizeof(sc.name), name, kSceneNameMax);
  r->sceneCount++;
}

inline bool recipeFromObject(const char *obj, HueRecipe *out) {
  if (!obj || !out) {
    return false;
  }
  char pageId[16];
  char event[16];
  char action[16];
  if (!jsonGetString(obj, "pageId", pageId, sizeof(pageId)) || !pageId[0]) {
    return false;
  }
  if (!jsonGetString(obj, "event", event, sizeof(event)) || !recipeEventOk(event)) {
    return false;
  }
  if (!jsonGetString(obj, "action", action, sizeof(action)) || !recipeActionOk(action)) {
    return false;
  }
  memset(out, 0, sizeof(*out));
  recipeCopyField(out->pageId, sizeof(out->pageId), pageId);
  recipeCopyField(out->event, sizeof(out->event), event);
  recipeCopyField(out->action, sizeof(out->action), action);

  if (strcmp(action, "recall_scene") == 0) {
    jsonEachArrayObject(obj, "targets", recipeParseSceneTarget, out);
    if (out->sceneCount < 1) {
      return false;
    }
    recipeCopyField(out->rtype, sizeof(out->rtype), "scene");
    recipeCopyField(out->rid, sizeof(out->rid), out->scenes[0].rid);
    return true;
  }

  char rtype[16];
  char rid[40];
  if (!jsonGetObjectString(obj, "target", "rtype", rtype, sizeof(rtype)) || !recipeRtypeOk(rtype)) {
    return false;
  }
  if (!jsonGetObjectString(obj, "target", "rid", rid, sizeof(rid)) || !rid[0]) {
    return false;
  }
  if (strcmp(rtype, "scene") == 0) {
    return false;
  }
  recipeCopyField(out->rtype, sizeof(out->rtype), rtype);
  recipeCopyField(out->rid, sizeof(out->rid), rid);
  return true;
}

inline void recipesParseOne(const char *obj, void *ctx) {
  uint8_t *n = static_cast<uint8_t *>(ctx);
  if (!n || *n >= kMaxRecipes) {
    return;
  }
  HueRecipe rec;
  if (!recipeFromObject(obj, &rec)) {
    return;
  }
  gRecipes[*n] = rec;
  (*n)++;
}

inline bool recipesParseArray(const char *json, uint8_t *countOut) {
  uint8_t n = countOut ? *countOut : 0;
  if (!json) {
    if (countOut) {
      *countOut = n;
    }
    return false;
  }
  if (!strchr(json, '[')) {
    return false;
  }
  jsonEachArrayObject(json, "recipes", recipesParseOne, &n);
  if (json[0] == '[') {
    const char *p = json;
    const char *start = nullptr;
    int depth = 0;
    bool inString = false;
    bool escape = false;
    for (; *p; p++) {
      const char c = *p;
      if (depth == 0 && !inString) {
        if (c == ']') {
          break;
        }
        if (c == '{') {
          depth = 1;
          start = p;
          inString = false;
          escape = false;
        }
        continue;
      }
      if (escape) {
        escape = false;
        continue;
      }
      if (inString) {
        if (c == '\\') {
          escape = true;
        } else if (c == '"') {
          inString = false;
        }
        continue;
      }
      if (c == '"') {
        inString = true;
        continue;
      }
      if (c == '{') {
        depth++;
      } else if (c == '}') {
        depth--;
        if (depth == 0 && start) {
          const size_t len = static_cast<size_t>(p - start + 1);
          char *tmp = static_cast<char *>(malloc(len + 1));
          if (tmp) {
            memcpy(tmp, start, len);
            tmp[len] = 0;
            recipesParseOne(tmp, &n);
            free(tmp);
          }
          start = nullptr;
        }
      }
    }
  }
  if (countOut) {
    *countOut = n;
  }
  return true;
}

static const char *kRecipeJsonKeys[] = {"json", "j1", "j2", "j3"};
static const uint8_t kRecipeJsonParts = 4;

inline void recipesSaveJson(Preferences &prefs) {
  uint8_t idx = 0;
  for (uint8_t part = 0; part < kRecipeJsonParts; part++) {
    if (idx >= gRecipeCount) {
      prefs.putString(kRecipeJsonKeys[part], "");
      continue;
    }
    uint8_t take = 0;
    String chunk = "[]";
    while (idx + take < gRecipeCount) {
      const String next = recipesToJsonRange(idx, static_cast<uint8_t>(idx + take + 1));
      if (take > 0 && next.length() >= kNvsStrMax) {
        break;
      }
      chunk = next;
      take++;
      if (chunk.length() >= kNvsStrMax) {
        break;
      }
    }
    prefs.putString(kRecipeJsonKeys[part], chunk);
    idx = static_cast<uint8_t>(idx + take);
  }
}

inline void recipesSave() {
  Preferences prefs;
  prefs.begin("recipes", false);
  prefs.putUInt("rev", gRecipeRev);
  prefs.putString("bid", gRecipeBridgeId);
  recipesSaveJson(prefs);
  prefs.end();
}

inline void recipesClear() {
  gRecipeCount = 0;
  gRecipeRev = 0;
  recipesSave();
}

inline void recipesForgetSaved() {
  Preferences prefs;
  if (prefs.begin("recipes", false)) {
    prefs.clear();
    prefs.end();
  }
  gRecipeCount = 0;
  gRecipeRev = 0;
  gRecipeBridgeId = "";
  gRecipesBidReset = false;
  gNeedConsoleSync = false;
}

inline void recipesLoad() {
  Preferences prefs;
  prefs.begin("recipes", true);
  gRecipeRev = prefs.getUInt("rev", 0);
  gRecipeBridgeId = prefs.getString("bid", "");
  gRecipeCount = 0;
  for (uint8_t part = 0; part < kRecipeJsonParts; part++) {
    const String json = prefs.getString(kRecipeJsonKeys[part], part == 0 ? "[]" : "");
    if (!json.length() || json == "[]") {
      continue;
    }
    uint8_t n = gRecipeCount;
    recipesParseArray(json.c_str(), &n);
    gRecipeCount = n;
  }
  prefs.end();
  LOG("NVS recipes rev=%u count=%u\n", gRecipeRev, gRecipeCount);
}

inline void recipesBindBridge(const String &bid) {
  if (!bid.length()) {
    return;
  }
  const bool mismatch = (gRecipeBridgeId.length() && !gRecipeBridgeId.equalsIgnoreCase(bid)) ||
                        (!gRecipeBridgeId.length() && (gRecipeCount > 0 || gRecipeRev > 0));
  if (mismatch) {
    LOGLN("Bridge id changed — dropping recipes");
    gRecipeBridgeId = bid;
    recipesClear();
    gRecipesBidReset = true;
    return;
  }
  if (gRecipeBridgeId != bid) {
    gRecipeBridgeId = bid;
    recipesSave();
  }
}

inline const HueRecipe *recipesFind(const char *pageId, const char *event) {
  if (!pageId || !event) {
    return nullptr;
  }
  for (uint8_t i = 0; i < gRecipeCount; i++) {
    if (strcmp(gRecipes[i].pageId, pageId) == 0 && strcmp(gRecipes[i].event, event) == 0) {
      return &gRecipes[i];
    }
  }
  return nullptr;
}

inline bool recipeIsChildLight(const HueRecipe *r) {
  return r && r->rid[0] && strcmp(r->rtype, "light") == 0 &&
         (strcmp(r->action, "toggle") == 0 || strcmp(r->action, "on") == 0 ||
          strcmp(r->action, "off") == 0);
}

// Tap and double are two different child lights: split fill, state per rid.
inline bool recipesTwoChildLights(const char *pageId, const HueRecipe **tap, const HueRecipe **dbl) {
  const HueRecipe *a = recipesFind(pageId, "short");
  const HueRecipe *b = recipesFind(pageId, "double_click");
  if (!recipeIsChildLight(a) || !recipeIsChildLight(b) || strcmp(a->rid, b->rid) == 0) {
    return false;
  }
  if (tap) {
    *tap = a;
  }
  if (dbl) {
    *dbl = b;
  }
  return true;
}

inline const HueRecipe *recipesFindScene(const char *pageId) {
  const HueRecipe *r = recipesFind(pageId, "short");
  if (r && strcmp(r->action, "recall_scene") == 0 && r->sceneCount > 0) {
    return r;
  }
  r = recipesFind(pageId, "double_click");
  if (r && strcmp(r->action, "recall_scene") == 0 && r->sceneCount > 0) {
    return r;
  }
  return nullptr;
}

inline bool recipesParseConfig(const char *body, uint32_t *revOut) {
  if (!body || !revOut) {
    return false;
  }
  if (!jsonHasKey(body, "rev") || !strstr(body, "\"recipes\"") || !strstr(body, "\"pages\"")) {
    return false;
  }
  const int rev = jsonGetInt(body, "rev", -1);
  if (rev < 0) {
    return false;
  }
  char keepId[16];
  keepId[0] = 0;
  if (pagesActive()) {
    recipeCopyField(keepId, sizeof(keepId), pagesActive()->id);
  }
  if (!pagesParseConfig(body)) {
    return false;
  }
  gRecipeCount = 0;
  uint8_t n = 0;
  jsonEachArrayObject(body, "recipes", recipesParseOne, &n);
  gRecipeCount = n;
  if (keepId[0]) {
    bool found = false;
    for (uint8_t i = 0; i < gPageCount; i++) {
      if (strcmp(gPages[i].id, keepId) == 0) {
        gPageIndex = i;
        found = true;
        break;
      }
    }
    if (!found) {
      gPageIndex = 0;
    }
  }
  pagesClampIndex();
  *revOut = static_cast<uint32_t>(rev);
  return true;
}
