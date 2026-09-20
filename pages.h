#pragma once

#include <Preferences.h>
#include <string.h>
#include "json_util.h"

// Páginas del círculo: NVS aparte de recetas para no pasar de 4000 B en un putString.

static const uint8_t kMaxPages = 6;
static const uint8_t kPageNameMax = 12;
static const uint8_t kSceneNameMax = 24;

enum PageSwipeAxis { PAGE_SWIPE_HORIZONTAL = 0, PAGE_SWIPE_VERTICAL = 1 };

enum PageDimMode { PAGE_DIM_NONE = 0, PAGE_DIM_GROUP = 1, PAGE_DIM_LIGHTS = 2 };

static const uint8_t kMaxDimLights = 2;

struct PageGroup {
  char rtype[16];
  char rid[40];
  char groupedLightRid[40];
};

struct Page {
  char id[16];
  char name[13];
  char theme[16];
  PageGroup group;
  PageDimMode dimMode;
  char dimGroupRid[40];
  uint8_t dimLightCount;
  char dimLights[kMaxDimLights][40];
};

struct PageTheme {
  const char *id;
  uint16_t bg;
  uint16_t ink;
  uint16_t mute;
  uint16_t fillOn;
  uint16_t fillOff;
  uint16_t accent;
  uint16_t ringTrack;
  uint16_t error;
};

// Hex de docs/round-themes.html → RGB565.
static constexpr uint16_t hx(uint32_t h) {
  return static_cast<uint16_t>((((h >> 16) & 0xF8u) << 8) | (((h >> 8) & 0xFCu) << 3) |
                               ((h & 0xFFu) >> 3));
}

static const PageTheme kPageThemes[] = {
    {"ember", hx(0x101410), hx(0xeee8e0), hx(0x7c7a74), hx(0xa54a00), hx(0x212021), hx(0xff9e00),
     hx(0x313131), hx(0xf80000)},
    {"night", hx(0x050506), hx(0xf4f1ea), hx(0xd8cfc4), hx(0xc47a10), hx(0x121214), hx(0xffb020),
     hx(0x2a2a22), hx(0xff5555)},
    {"coal", hx(0x0a0a0a), hx(0xd8d0c8), hx(0x6a6660), hx(0x5c2e12), hx(0x161616), hx(0xc45c18),
     hx(0x242424), hx(0xc04040)},
    {"graphite", hx(0x121314), hx(0xecece8), hx(0x9a9ca0), hx(0x6a5a40), hx(0x1c1d1f), hx(0xd4b483),
     hx(0x2e3035), hx(0xd9897c)},
    {"ink", hx(0x0b1016), hx(0xe8eef6), hx(0x8fa0b5), hx(0x0e4a48), hx(0x121a24), hx(0x3dd6c6),
     hx(0x243040), hx(0xf07a72)},
    {"ocean", hx(0x071018), hx(0xdceef8), hx(0x7fa3bb), hx(0x0a4a6a), hx(0x0d1b28), hx(0x38bdf8),
     hx(0x1c3348), hx(0xfb7185)},
    {"nord", hx(0x2e3440), hx(0xeceff4), hx(0xa0a8b8), hx(0x4c566a), hx(0x3b4252), hx(0x88c0d0),
     hx(0x434c5e), hx(0xbf616a)},
    {"plum", hx(0x140f14), hx(0xf3e8ee), hx(0xb39aa8), hx(0x6a3048), hx(0x1d161d), hx(0xe8a0b4),
     hx(0x3a2c36), hx(0xe07a7a)},
    {"violet", hx(0x120e18), hx(0xf0e8ff), hx(0xa090b8), hx(0x4a3470), hx(0x1a1524), hx(0xc4a0ff),
     hx(0x322848), hx(0xff6a8a)},
    {"dracula", hx(0x1e1f29), hx(0xf8f8f2), hx(0xb0b3c8), hx(0x5a3d78), hx(0x282a36), hx(0xbd93f9),
     hx(0x44475a), hx(0xff5555)},
    {"matrix", hx(0x020402), hx(0xd0ffd4), hx(0x5f9e68), hx(0x0a3a14), hx(0x071208), hx(0x00ff41),
     hx(0x1a3d1e), hx(0xff4d4d)},
    {"forest", hx(0x0c120e), hx(0xe8f0e6), hx(0x8fa88c), hx(0x3a4a18), hx(0x151e17), hx(0xc9a227),
     hx(0x2a3a2c), hx(0xe07a6a)},
    {"copper", hx(0x16100c), hx(0xf4ece4), hx(0xb09078), hx(0x8a4020), hx(0x241810), hx(0xe07a3d),
     hx(0x3a2a20), hx(0xe06050)},
    {"snow", hx(0xe8eef4), hx(0x1a2330), hx(0x5c6b7a), hx(0xc5d8f0), hx(0xf4f6f8), hx(0x2563eb),
     hx(0xc8d4e0), hx(0xb42318)},
    {"paper", hx(0xe8e0d4), hx(0x1c1814), hx(0x6e675c), hx(0xe8c4a0), hx(0xf3eee4), hx(0xc45c26),
     hx(0xd4c8b8), hx(0xa33b2a)},
    {"sand", hx(0xe8dcc8), hx(0x2a2118), hx(0x7a6a58), hx(0xe0b090), hx(0xf4ecdf), hx(0xb85c38),
     hx(0xd4c4a8), hx(0xa33b2a)},
    {"linen", hx(0xe6e2d4), hx(0x2a2a1c), hx(0x6e6e58), hx(0xc8c4a0), hx(0xf2efe4), hx(0x6a7a38),
     hx(0xd0ccb8), hx(0xa04030)},
    {"meadow", hx(0xdce8d8), hx(0x1c2a1c), hx(0x5a7058), hx(0xa8c898), hx(0xe8f0e4), hx(0x3d8a4a),
     hx(0xc0d4bc), hx(0xb04030)},
    {"sky", hx(0xd4e4f0), hx(0x183040), hx(0x5a7088), hx(0xa0c8e8), hx(0xe8f2f8), hx(0x2a8ad4),
     hx(0xb8d0e0), hx(0xc04040)},
    {"porcelain", hx(0xf0e4e4), hx(0x3a2028), hx(0x8a6870), hx(0xe8b8c0), hx(0xf8eeee), hx(0xc45a70),
     hx(0xe0c8cc), hx(0xb03030)},
};

static const size_t kPageThemeCount = sizeof(kPageThemes) / sizeof(kPageThemes[0]);

inline Page gPages[kMaxPages];
inline uint8_t gPageCount = 0;
inline uint8_t gPageIndex = 0;
inline PageSwipeAxis gPageSwipeAxis = PAGE_SWIPE_HORIZONTAL;
inline String gPageBridgeId;
inline char gLastSceneRid[kMaxPages][40];
inline bool gNeedHueState = false;
inline bool gNeedFullPaint = false;

static const uint16_t kScreenTimeoutDefault = 30;
static const uint16_t kScreenTimeoutMin = 10;
static const uint16_t kScreenTimeoutMax = 600;
inline uint16_t gScreenTimeoutSec = kScreenTimeoutDefault;

inline uint16_t pagesClampTimeout(int v) {
  if (v == 0) {
    return 0;
  }
  if (v < (int)kScreenTimeoutMin) {
    return kScreenTimeoutMin;
  }
  if (v > (int)kScreenTimeoutMax) {
    return kScreenTimeoutMax;
  }
  return static_cast<uint16_t>(v);
}

inline void pagesSaveTimeout() {
  Preferences prefs;
  prefs.begin("pages", false);
  prefs.putUShort("sto", gScreenTimeoutSec);
  prefs.end();
}

// Campo ausente: no toca NVS / valor actual. 0 = always on; el resto se clampa a 10–600.
inline bool pagesParseTimeout(const char *body) {
  if (!body || !jsonHasKey(body, "screenTimeoutSec")) {
    return false;
  }
  const uint16_t t = pagesClampTimeout(jsonGetInt(body, "screenTimeoutSec", (int)kScreenTimeoutDefault));
  if (t == gScreenTimeoutSec) {
    return false;
  }
  gScreenTimeoutSec = t;
  return true;
}

inline void pageCopyField(char *dst, size_t n, const char *src) {
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

inline uint16_t colorMix565(uint16_t a, uint16_t b, uint8_t aPct) {
  if (aPct >= 100) {
    return a;
  }
  if (aPct == 0) {
    return b;
  }
  const uint8_t bPct = static_cast<uint8_t>(100 - aPct);
  const uint8_t ar = (a >> 11) & 0x1F;
  const uint8_t ag = (a >> 5) & 0x3F;
  const uint8_t ab = a & 0x1F;
  const uint8_t br = (b >> 11) & 0x1F;
  const uint8_t bg = (b >> 5) & 0x3F;
  const uint8_t bb = b & 0x1F;
  const uint8_t r = static_cast<uint8_t>((ar * aPct + br * bPct) / 100);
  const uint8_t g = static_cast<uint8_t>((ag * aPct + bg * bPct) / 100);
  const uint8_t bl = static_cast<uint8_t>((ab * aPct + bb * bPct) / 100);
  return static_cast<uint16_t>((r << 11) | (g << 5) | bl);
}

inline const PageTheme *themeFind(const char *id) {
  if (id && id[0]) {
    for (size_t i = 0; i < kPageThemeCount; i++) {
      if (strcmp(kPageThemes[i].id, id) == 0) {
        return &kPageThemes[i];
      }
    }
  }
  return &kPageThemes[0];  // ember
}

inline const PageTheme *themeEmber() { return &kPageThemes[0]; }

inline bool pageRtypeDimOk(const char *r) {
  return r && (strcmp(r, "light") == 0 || strcmp(r, "grouped_light") == 0);
}

inline bool pageGroupRtypeOk(const char *r) {
  return r && (strcmp(r, "room") == 0 || strcmp(r, "zone") == 0);
}

inline void pagesClearLastScenes() {
  memset(gLastSceneRid, 0, sizeof(gLastSceneRid));
}

inline void pagesEnsureDefault() {
  if (gPageCount > 0) {
    return;
  }
  memset(&gPages[0], 0, sizeof(gPages[0]));
  pageCopyField(gPages[0].id, sizeof(gPages[0].id), "p1");
  pageCopyField(gPages[0].name, sizeof(gPages[0].name), "Page 1");
  pageCopyField(gPages[0].theme, sizeof(gPages[0].theme), "ember");
  gPageCount = 1;
  gPageIndex = 0;
  gPageSwipeAxis = PAGE_SWIPE_HORIZONTAL;
}

inline const Page *pagesActive() {
  if (gPageCount == 0 || gPageIndex >= gPageCount) {
    return nullptr;
  }
  return &gPages[gPageIndex];
}

inline const char *pagesActiveId() {
  const Page *p = pagesActive();
  return p ? p->id : "";
}

inline const PageTheme *pagesActiveTheme() {
  const Page *p = pagesActive();
  return p ? themeFind(p->theme) : themeEmber();
}

inline bool pagesHasId(const char *id) {
  if (!id || !id[0]) {
    return false;
  }
  for (uint8_t i = 0; i < gPageCount; i++) {
    if (strcmp(gPages[i].id, id) == 0) {
      return true;
    }
  }
  return false;
}

inline bool pagesHasDim() {
  const Page *p = pagesActive();
  if (!p) {
    return false;
  }
  if (p->dimMode == PAGE_DIM_GROUP) {
    return p->dimGroupRid[0] != 0;
  }
  if (p->dimMode == PAGE_DIM_LIGHTS) {
    return p->dimLightCount > 0 && p->dimLights[0][0] != 0;
  }
  return false;
}

inline const char *pagesLastSceneRid() {
  if (gPageIndex >= kMaxPages) {
    return "";
  }
  return gLastSceneRid[gPageIndex];
}

inline void pagesSetLastSceneRid(const char *rid) {
  if (gPageIndex >= kMaxPages) {
    return;
  }
  pageCopyField(gLastSceneRid[gPageIndex], sizeof(gLastSceneRid[0]), rid);
  Preferences prefs;
  prefs.begin("pages", false);
  char key[4] = {'s', static_cast<char>('0' + gPageIndex), 0, 0};
  prefs.putString(key, gLastSceneRid[gPageIndex]);
  prefs.end();
}

inline void pagesClampIndex() {
  if (gPageCount == 0) {
    gPageIndex = 0;
    return;
  }
  if (gPageIndex >= gPageCount) {
    gPageIndex = 0;
  }
}

inline String pagesToJson() {
  String s = "[";
  for (uint8_t i = 0; i < gPageCount; i++) {
    if (i) {
      s += ',';
    }
    s += "{\"id\":";
    jsonAppendEscaped(s, gPages[i].id);
    s += ",\"name\":";
    jsonAppendEscaped(s, gPages[i].name);
    s += ",\"theme\":";
    jsonAppendEscaped(s, gPages[i].theme);
    if (gPages[i].group.rid[0] || gPages[i].group.groupedLightRid[0]) {
      s += ",\"group\":{\"rtype\":";
      jsonAppendEscaped(s, gPages[i].group.rtype[0] ? gPages[i].group.rtype : "room");
      s += ",\"rid\":";
      jsonAppendEscaped(s, gPages[i].group.rid);
      s += ",\"groupedLightRid\":";
      jsonAppendEscaped(s, gPages[i].group.groupedLightRid);
      s += '}';
    }
    if (gPages[i].dimMode == PAGE_DIM_GROUP && gPages[i].dimGroupRid[0]) {
      s += ",\"dim\":{\"mode\":\"group\",\"rid\":";
      jsonAppendEscaped(s, gPages[i].dimGroupRid);
      s += '}';
    } else if (gPages[i].dimMode == PAGE_DIM_LIGHTS && gPages[i].dimLightCount > 0) {
      s += ",\"dim\":{\"mode\":\"lights\",\"rids\":[";
      for (uint8_t j = 0; j < gPages[i].dimLightCount; j++) {
        if (j) {
          s += ',';
        }
        jsonAppendEscaped(s, gPages[i].dimLights[j]);
      }
      s += "]}";
    }
    s += '}';
  }
  s += "]";
  return s;
}

inline void pageParseDimRid(const char *s, void *ctx) {
  Page *out = static_cast<Page *>(ctx);
  if (!out || !s || !s[0] || out->dimLightCount >= kMaxDimLights) {
    return;
  }
  for (uint8_t i = 0; i < out->dimLightCount; i++) {
    if (strcmp(out->dimLights[i], s) == 0) {
      return;
    }
  }
  pageCopyField(out->dimLights[out->dimLightCount], sizeof(out->dimLights[0]), s);
  out->dimLightCount++;
}

inline void pageParseDim(const char *obj, Page *out) {
  if (!obj || !out) {
    return;
  }
  const char *dimObj = jsonObjectPtr(obj, "dim");
  if (dimObj) {
    char mode[16];
    mode[0] = 0;
    jsonGetString(dimObj, "mode", mode, sizeof(mode));
    if (strcmp(mode, "group") == 0) {
      char rid[40];
      rid[0] = 0;
      jsonGetString(dimObj, "rid", rid, sizeof(rid));
      if (rid[0]) {
        out->dimMode = PAGE_DIM_GROUP;
        pageCopyField(out->dimGroupRid, sizeof(out->dimGroupRid), rid);
        return;
      }
    } else if (strcmp(mode, "lights") == 0) {
      jsonEachArrayString(dimObj, "rids", pageParseDimRid, out);
      if (out->dimLightCount > 0) {
        out->dimMode = PAGE_DIM_LIGHTS;
        return;
      }
    }
  }
  // Compat NVS viejo: dimTarget de un rid → group hasta el próximo poll.
  char rtype[16];
  char rid[40];
  rtype[0] = 0;
  rid[0] = 0;
  jsonGetObjectString(obj, "dimTarget", "rtype", rtype, sizeof(rtype));
  jsonGetObjectString(obj, "dimTarget", "rid", rid, sizeof(rid));
  if (rid[0] && pageRtypeDimOk(rtype)) {
    out->dimMode = PAGE_DIM_GROUP;
    pageCopyField(out->dimGroupRid, sizeof(out->dimGroupRid), rid);
  }
}

inline bool pageFromObject(const char *obj, Page *out) {
  if (!obj || !out) {
    return false;
  }
  char id[16];
  char name[48];
  char theme[16];
  if (!jsonGetString(obj, "id", id, sizeof(id)) || !id[0]) {
    return false;
  }
  memset(out, 0, sizeof(*out));
  pageCopyField(out->id, sizeof(out->id), id);
  name[0] = 0;
  jsonGetString(obj, "name", name, sizeof(name));
  asciiFoldClip(out->name, sizeof(out->name), name[0] ? name : id, kPageNameMax);
  theme[0] = 0;
  jsonGetString(obj, "theme", theme, sizeof(theme));
  pageCopyField(out->theme, sizeof(out->theme), theme[0] ? theme : "ember");
  char gtype[16];
  char grid[40];
  char glrid[40];
  gtype[0] = 0;
  grid[0] = 0;
  glrid[0] = 0;
  jsonGetObjectString(obj, "group", "rtype", gtype, sizeof(gtype));
  jsonGetObjectString(obj, "group", "rid", grid, sizeof(grid));
  jsonGetObjectString(obj, "group", "groupedLightRid", glrid, sizeof(glrid));
  if (pageGroupRtypeOk(gtype)) {
    pageCopyField(out->group.rtype, sizeof(out->group.rtype), gtype);
  }
  pageCopyField(out->group.rid, sizeof(out->group.rid), grid);
  pageCopyField(out->group.groupedLightRid, sizeof(out->group.groupedLightRid), glrid);
  pageParseDim(obj, out);
  return true;
}

inline void pagesParseOne(const char *obj, void *ctx) {
  uint8_t *n = static_cast<uint8_t *>(ctx);
  if (!n || *n >= kMaxPages) {
    return;
  }
  Page p;
  if (!pageFromObject(obj, &p)) {
    return;
  }
  gPages[*n] = p;
  (*n)++;
}

inline bool pagesParseArray(const char *json, uint8_t *countOut) {
  uint8_t n = 0;
  if (!json) {
    if (countOut) {
      *countOut = 0;
    }
    return false;
  }
  jsonEachArrayObject(json, "pages", pagesParseOne, &n);
  if (n == 0 && json[0] == '[') {
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
            pagesParseOne(tmp, &n);
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

inline void pagesSaveIndex() {
  Preferences prefs;
  prefs.begin("pages", false);
  prefs.putUChar("idx", gPageIndex);
  prefs.end();
}

inline void pagesSave() {
  Preferences prefs;
  prefs.begin("pages", false);
  prefs.putString("json", pagesToJson());
  prefs.putString("axis", gPageSwipeAxis == PAGE_SWIPE_VERTICAL ? "vertical" : "horizontal");
  prefs.putUShort("sto", gScreenTimeoutSec);
  prefs.putString("bid", gPageBridgeId);
  prefs.putUChar("idx", gPageIndex);
  for (uint8_t i = 0; i < kMaxPages; i++) {
    char key[4] = {'s', static_cast<char>('0' + i), 0, 0};
    prefs.putString(key, gLastSceneRid[i]);
  }
  prefs.end();
}

inline void pagesClear() {
  gPageCount = 0;
  gPageIndex = 0;
  gPageSwipeAxis = PAGE_SWIPE_HORIZONTAL;
  pagesClearLastScenes();
  pagesEnsureDefault();
  pagesSave();
}

inline void pagesLoad() {
  Preferences prefs;
  prefs.begin("pages", true);
  gPageBridgeId = prefs.getString("bid", "");
  const String json = prefs.getString("json", "[]");
  const String axis = prefs.getString("axis", "horizontal");
  gScreenTimeoutSec = pagesClampTimeout((int)prefs.getUShort("sto", kScreenTimeoutDefault));
  gPageIndex = prefs.getUChar("idx", 0);
  for (uint8_t i = 0; i < kMaxPages; i++) {
    char key[4] = {'s', static_cast<char>('0' + i), 0, 0};
    const String s = prefs.getString(key, "");
    pageCopyField(gLastSceneRid[i], sizeof(gLastSceneRid[0]), s.c_str());
  }
  prefs.end();
  gPageSwipeAxis = axis.equals("vertical") ? PAGE_SWIPE_VERTICAL : PAGE_SWIPE_HORIZONTAL;
  gPageCount = 0;
  uint8_t n = 0;
  pagesParseArray(json.c_str(), &n);
  gPageCount = n;
  pagesEnsureDefault();
  pagesClampIndex();
  LOG("NVS pages count=%u idx=%u axis=%s timeout=%u\n", gPageCount, gPageIndex,
                gPageSwipeAxis == PAGE_SWIPE_VERTICAL ? "vertical" : "horizontal", gScreenTimeoutSec);
}

inline void pagesBindBridge(const String &bid) {
  if (!bid.length()) {
    return;
  }
  if (gPageBridgeId.length() && !gPageBridgeId.equalsIgnoreCase(bid)) {
    LOGLN("Bridge id changed — dropping pages");
    gPageBridgeId = bid;
    pagesClear();
    return;
  }
  if (gPageBridgeId != bid) {
    gPageBridgeId = bid;
    pagesSave();
  }
}

inline bool pagesParseAxis(const char *body) {
  char axis[16];
  axis[0] = 0;
  if (jsonGetString(body, "pageSwipeAxis", axis, sizeof(axis)) && strcmp(axis, "vertical") == 0) {
    gPageSwipeAxis = PAGE_SWIPE_VERTICAL;
  } else {
    gPageSwipeAxis = PAGE_SWIPE_HORIZONTAL;
  }
  return true;
}

inline bool pagesParseConfig(const char *body) {
  if (!body || !strstr(body, "\"pages\"")) {
    return false;
  }
  gPageCount = 0;
  uint8_t n = 0;
  jsonEachArrayObject(body, "pages", pagesParseOne, &n);
  gPageCount = n;
  pagesParseAxis(body);
  pagesParseTimeout(body);
  pagesEnsureDefault();
  pagesClampIndex();
  return true;
}

inline void pagesGo(int delta) {
  if (gPageCount <= 1) {
    return;
  }
  const int n = static_cast<int>(gPageCount);
  int idx = static_cast<int>(gPageIndex) + delta;
  idx %= n;
  if (idx < 0) {
    idx += n;
  }
  gPageIndex = static_cast<uint8_t>(idx);
  pagesSaveIndex();
  gNeedHueState = true;
}

inline void pagesNext() { pagesGo(1); }
inline void pagesPrev() { pagesGo(-1); }
