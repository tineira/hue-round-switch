// Host tests for the firmware's JSON helpers and config-poll parsers.
// Plain C++ on a PC, no board: see run.sh. Arduino types come from stubs/.

#include "log.h"
#include "json_util.h"
#include "pages.h"
#include "recipes.h"

#include <string>
#include <vector>

static int gFailures = 0;
static int gChecks = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    gChecks++;                                                                 \
    if (!(cond)) {                                                             \
      gFailures++;                                                             \
      fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
    }                                                                          \
  } while (0)

#define CHECK_STR(got, want)                                                                  \
  do {                                                                                        \
    gChecks++;                                                                                \
    const std::string g_ = (got);                                                             \
    const std::string w_ = (want);                                                            \
    if (g_ != w_) {                                                                           \
      gFailures++;                                                                            \
      fprintf(stderr, "%s:%d: %s\n  got:  \"%s\"\n  want: \"%s\"\n", __FILE__, __LINE__, #got, \
              g_.c_str(), w_.c_str());                                                        \
    }                                                                                         \
  } while (0)

// ---------------------------------------------------------------------------
// json_util.h: field extractors

static void testGetString() {
  char out[32];
  CHECK(jsonGetString("{\"id\":\"abc\",\"x\":1}", "id", out, sizeof(out)));
  CHECK_STR(out, "abc");
  CHECK(jsonGetString("{\"n\":\"a\\\"b\\\\c\\nd\"}", "n", out, sizeof(out)));
  CHECK_STR(out, "a\"b\\c\nd");
  CHECK(!jsonGetString("{\"id\":1}", "id", out, sizeof(out)));
  CHECK(!jsonGetString("{}", "id", out, sizeof(out)));
  // Truncates to the buffer, always terminated.
  char small[4];
  CHECK(jsonGetString("{\"id\":\"abcdef\"}", "id", small, sizeof(small)));
  CHECK_STR(small, "abc");
}

static void testObjectString() {
  const char *j =
      "{\"id\":\"s1\",\"group\":{\"rid\":\"g1\",\"rtype\":\"room\"},\"metadata\":{\"name\":\"Relax\"}}";
  char out[32];
  CHECK(jsonGetObjectString(j, "group", "rtype", out, sizeof(out)));
  CHECK_STR(out, "room");
  CHECK(jsonGetObjectString(j, "metadata", "name", out, sizeof(out)));
  CHECK_STR(out, "Relax");
  CHECK(!jsonGetObjectString("{\"group\":null}", "group", "rid", out, sizeof(out)));
  CHECK(jsonObjectPtr(j, "group") != nullptr);
  CHECK(jsonObjectPtr("{\"dim\":null}", "dim") == nullptr);
  CHECK(jsonHasKey(j, "metadata"));
  CHECK(!jsonHasKey(j, "dim"));
}

static void testGetInt() {
  CHECK(jsonGetInt("{\"rev\":42}", "rev", -1) == 42);
  CHECK(jsonGetInt("{\"rev\": -3}", "rev", 0) == -3);
  CHECK(jsonGetInt("{\"rev\":\"7\"}", "rev", -1) == -1);
  CHECK(jsonGetInt("{}", "rev", 9) == 9);
}

static void testHueState() {
  bool on = false;
  CHECK(jsonHueOn("{\"id\":\"x\",\"on\":{\"on\":true},\"dimming\":{\"brightness\":55.4}}", &on));
  CHECK(on);
  CHECK(jsonHueOn("{\"on\":{\"on\":false}}", &on));
  CHECK(!on);
  CHECK(!jsonHueOn("{\"id\":\"x\"}", &on));

  int pct = -1;
  CHECK(jsonHueBrightness("{\"dimming\":{\"brightness\":55.6}}", &pct));
  CHECK(pct == 56);
  CHECK(jsonHueBrightness("{\"dimming\":{\"brightness\":140}}", &pct));
  CHECK(pct == 100);
  CHECK(!jsonHueBrightness("{\"on\":{\"on\":true}}", &pct));

  bool active = false;
  CHECK(jsonHueSceneActive("{\"status\":{\"active\":\"static\"}}", &active));
  CHECK(active);
  CHECK(jsonHueSceneActive("{\"status\":{\"active\":\"inactive\"}}", &active));
  CHECK(!active);
  CHECK(!jsonHueSceneActive("{\"id\":\"s\"}", &active));
}

static void testFindRidByRtype() {
  char out[40];
  const char *room =
      "{\"id\":\"r1\",\"services\":[{\"rid\":\"gl-1\",\"rtype\":\"grouped_light\"}],"
      "\"children\":[{\"rid\":\"d1\",\"rtype\":\"device\"}]}";
  CHECK(jsonFindRidByRtype(room, "grouped_light", out, sizeof(out)));
  CHECK_STR(out, "gl-1");
  CHECK(jsonFindRidByRtype(room, "device", out, sizeof(out)));
  CHECK_STR(out, "d1");
  CHECK(!jsonFindRidByRtype(room, "light", out, sizeof(out)));
}

// The circle set, as the console spells it (ROUND_CIRCLE_CHARS) and its CP437 codes
// (console docs/specs/round-accented-names.md §2), in the same order.
static const char *kCircleChars = "ÇüéâäàåçêëèïîìÄÅÉæÆôöòûùÿÖÜáíóúñÑ¿¡ß";
static const uint8_t kCircleCodes[] = {
    0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B,
    0x8C, 0x8D, 0x8E, 0x8F, 0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
    0x98, 0x99, 0x9A, 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA8, 0xAD, 0xE1};

static void testCircleFold() {
  char out[64];
  // ASCII and the circle set stay as UTF-8.
  circleFold(out, sizeof(out), "Niños");
  CHECK_STR(out, "Niños");
  circleFold(out, sizeof(out), "Canción");
  CHECK_STR(out, "Canción");
  circleFold(out, sizeof(out), "¿Qué?");
  CHECK_STR(out, "¿Qué?");
  circleFold(out, sizeof(out), "Ñandú");
  CHECK_STR(out, "Ñandú");
  circleFold(out, sizeof(out), "¡Hola! Größe Ærø");
  CHECK_STR(out, "¡Hola! Größe Æro");
  circleFold(out, sizeof(out), kCircleChars);
  CHECK_STR(out, kCircleChars);
  // Other Latin-1 / Latin Extended-A letters fold to the base letter. Capital Á, Í, Ó, Ú
  // have no glyph and become the plain capital.
  circleFold(out, sizeof(out), "Ángel Íñigo Óscar Úrsula");
  CHECK_STR(out, "Angel Iñigo Oscar Ursula");
  circleFold(out, sizeof(out), "Łazienka");
  CHECK_STR(out, "Lazienka");
  circleFold(out, sizeof(out), "Øre");
  CHECK_STR(out, "Ore");
  circleFold(out, sizeof(out), "Kuchyně");
  CHECK_STR(out, "Kuchyne");
  circleFold(out, sizeof(out), "Işık");
  CHECK_STR(out, "Isik");
  circleFold(out, sizeof(out), "Āčőžſ");  // both ends of Latin Extended-A
  CHECK_STR(out, "Acozs");
  circleFold(out, sizeof(out), "Đurđa Œuvre");
  CHECK_STR(out, "Durda Ouvre");
  circleFold(out, sizeof(out), "Ãõ Ðþ");
  CHECK_STR(out, "Ao Dt");
  // Dropped: other scripts, emoji, symbols, combining marks, × and ÷ (as the console does).
  circleFold(out, sizeof(out), "aαb");
  CHECK_STR(out, "ab");
  circleFold(out, sizeof(out), "Sala \xF0\x9F\x92\xA1!");  // emoji
  CHECK_STR(out, "Sala !");
  circleFold(out, sizeof(out), "2×2÷1 °C €");
  CHECK_STR(out, "221 C ");
  circleFold(out, sizeof(out), "n\xCC\x83");  // n + combining tilde (not NFC)
  CHECK_STR(out, "n");
  circleFold(out, sizeof(out), "a\tb\x7F" "c");
  CHECK_STR(out, "abc");
  // Broken UTF-8: stray bytes are skipped, the rest survives.
  circleFold(out, sizeof(out), "a\xC3(b\xA4" "c\xC3");
  CHECK_STR(out, "a(bc");
  // A full buffer never ends in half a character.
  char tiny[4];
  circleFold(tiny, sizeof(tiny), "Niño");
  CHECK_STR(tiny, "Ni");
  circleFold(tiny, sizeof(tiny), "ñañ");
  CHECK_STR(tiny, "ña");
}

static void testCircleFoldClip() {
  char page[25];  // Page.name
  circleFoldClip(page, sizeof(page), "Living room lamps", kPageNameMax);
  CHECK_STR(page, "Living room.");
  circleFoldClip(page, sizeof(page), "Short", kPageNameMax);
  CHECK_STR(page, "Short");
  // 12 characters, not bytes: twelve ñ (24 bytes) fit whole.
  circleFoldClip(page, sizeof(page), "ññññññññññññ", kPageNameMax);
  CHECK_STR(page, "ññññññññññññ");
  CHECK(utf8Chars(page) == 12);
  circleFoldClip(page, sizeof(page), "ñññññññññññññ", kPageNameMax);
  CHECK_STR(page, "ñññññññññññ.");
  CHECK(utf8Chars(page) == 12);
  circleFoldClip(page, sizeof(page), "Habitación niños", kPageNameMax);
  CHECK_STR(page, "Habitación .");
  CHECK(utf8Chars(page) == 12);
  // A buffer too small for the characters clips by bytes, never inside ñ.
  char small[13];
  circleFoldClip(small, sizeof(small), "ññññññññññññ", kPageNameMax);
  CHECK_STR(small, "ñññññ.");
  circleFoldClip(small, sizeof(small), "aññññññññññ", kPageNameMax);
  CHECK_STR(small, "añññññ.");
  char scene[49];  // RecipeScene.name
  circleFoldClip(scene, sizeof(scene), "Canción de cuna para niños pequeños", kSceneNameMax);
  CHECK_STR(scene, "Canción de cuna para ni.");
  CHECK(utf8Chars(scene) == 24);
  circleFoldClip(scene, sizeof(scene), "ÇüéâäàåçêëèïîìÄÅÉæÆôöòûù", kSceneNameMax);
  CHECK_STR(scene, "ÇüéâäàåçêëèïîìÄÅÉæÆôöòûù");  // 24 two-byte characters: 48 bytes
}

static void testCircleToCp437() {
  // Every circle-set entry maps to its CP437 code, one byte each.
  char out[64];
  circleToCp437(out, sizeof(out), kCircleChars);
  CHECK(strlen(out) == sizeof(kCircleCodes));
  for (size_t i = 0; i < sizeof(kCircleCodes) && out[i]; i++) {
    if (static_cast<uint8_t>(out[i]) != kCircleCodes[i]) {
      CHECK(static_cast<uint8_t>(out[i]) == kCircleCodes[i]);
      fprintf(stderr, "  circle set entry %zu\n", i);
    }
  }
  CHECK(sizeof(kCircleSet) / sizeof(kCircleSet[0]) == sizeof(kCircleCodes));
  CHECK(utf8Chars(kCircleChars) == sizeof(kCircleCodes));
  circleToCp437(out, sizeof(out), "Niños");
  CHECK_STR(out, "Ni\xA4os");
  circleToCp437(out, sizeof(out), "¿Qué?");
  CHECK_STR(out, "\xA8Qu\x82?");
  circleToCp437(out, sizeof(out), "Round 0.6.5");
  CHECK_STR(out, "Round 0.6.5");
  // Not in the circle set (not folded first): left out rather than drawn as garbage.
  circleToCp437(out, sizeof(out), "Ángel \xF0\x9F\x92\xA1");
  CHECK_STR(out, "ngel ");
  char tiny[3];
  circleToCp437(tiny, sizeof(tiny), "ñañ");
  CHECK_STR(tiny, "\xA4" "a");
}

static void testAppendEscaped() {
  String s;
  jsonAppendEscaped(s, "a\"b\\c\nd\x01");
  CHECK_STR(s.c_str(), "\"a\\\"b\\\\c\\nd\"");
  String n;
  jsonAppendEscaped(n, nullptr);
  CHECK_STR(n.c_str(), "\"\"");
  String u;
  jsonAppendEscaped(u, "Niños ¿sí?");  // UTF-8 bytes pass through unchanged
  CHECK_STR(u.c_str(), "\"Niños ¿sí?\"");
}

static void collect(const char *s, void *ctx) { static_cast<std::vector<std::string> *>(ctx)->push_back(s); }

static void testEachArray() {
  std::vector<std::string> v;
  jsonEachArrayString("{\"rids\":[\"a\",\"b\\\"c\"],\"x\":[\"no\"]}", "rids", collect, &v);
  CHECK(v.size() == 2);
  if (v.size() == 2) {
    CHECK_STR(v[0], "a");
    CHECK_STR(v[1], "b\"c");
  }

  v.clear();
  jsonEachArrayObject(
      "{\"pages\":[{\"id\":\"p1\",\"n\":\"}{\"},{\"id\":\"p2\",\"sub\":{\"a\":1}}],\"after\":[{}]}", "pages",
      collect, &v);
  CHECK(v.size() == 2);
  if (v.size() == 2) {
    CHECK_STR(v[0], "{\"id\":\"p1\",\"n\":\"}{\"}");
    CHECK_STR(v[1], "{\"id\":\"p2\",\"sub\":{\"a\":1}}");
  }
}

static void testEachTopLevel() {
  std::vector<std::string> v;
  // Nested objects, "}" and "{" inside strings, and escaped quotes stay inside one object.
  jsonEachTopLevelObject(
      "[{\"id\":\"p1\",\"n\":\"}{\"},{\"id\":\"p2\",\"sub\":{\"a\":{\"b\":1}}},{\"q\":\"x\\\"}\"}]", collect, &v);
  CHECK(v.size() == 3);
  if (v.size() == 3) {
    CHECK_STR(v[0], "{\"id\":\"p1\",\"n\":\"}{\"}");
    CHECK_STR(v[1], "{\"id\":\"p2\",\"sub\":{\"a\":{\"b\":1}}}");
    CHECK_STR(v[2], "{\"q\":\"x\\\"}\"}");
  }

  // Empty array.
  v.clear();
  jsonEachTopLevelObject("[]", collect, &v);
  CHECK(v.empty());

  // Stops at the closing ']': a later array is not walked.
  v.clear();
  jsonEachTopLevelObject("[ {\"a\":1} , {\"b\":[1,2]} ] [{\"c\":3}]", collect, &v);
  CHECK(v.size() == 2);
  if (v.size() == 2) {
    CHECK_STR(v[0], "{\"a\":1}");
    CHECK_STR(v[1], "{\"b\":[1,2]}");
  }

  // Not a top-level array: nothing, even if an array appears later.
  v.clear();
  jsonEachTopLevelObject("{\"pages\":[{\"a\":1}]}", collect, &v);
  jsonEachTopLevelObject(" [{\"a\":1}]", collect, &v);
  jsonEachTopLevelObject("", collect, &v);
  jsonEachTopLevelObject(nullptr, collect, &v);
  CHECK(v.empty());

  // Unterminated: complete objects are handed over, the cut one is not.
  v.clear();
  jsonEachTopLevelObject("[{\"a\":1},{\"b\":", collect, &v);
  CHECK(v.size() == 1);
}

// ---------------------------------------------------------------------------
// json_util.h: JsonDataSink (streamed Clip v2 body)

struct SinkResult {
  std::vector<std::string> objs;
  int objects = 0;
  bool overflow = false;
};

// Feeds the body in chunks of `chunk` bytes (1 = byte by byte, like HTTPClient can).
static SinkResult runSink(const std::string &body, size_t chunk) {
  SinkResult r;
  JsonDataSink sink;
  sink.onObject = collect;
  sink.ctx = &r.objs;
  for (size_t i = 0; i < body.size(); i += chunk) {
    const size_t n = body.size() - i < chunk ? body.size() - i : chunk;
    if (n == 1) {
      sink.write(static_cast<uint8_t>(body[i]));
    } else {
      sink.write(reinterpret_cast<const uint8_t *>(body.data() + i), n);
    }
  }
  r.objects = sink.objects;
  r.overflow = sink.overflow;
  return r;
}

static void testSinkBasic() {
  const std::string body =
      "{\"errors\":[],\"data\" : [ {\"id\":\"a\",\"s\":\"{[\\\"]}\"} ,\n"
      "{\"id\":\"b\",\"nested\":{\"data\":[1,2]}} ]}";
  for (size_t chunk : {size_t(1), size_t(3), size_t(64), body.size()}) {
    SinkResult r = runSink(body, chunk);
    CHECK(r.objects == 2);
    CHECK(r.objs.size() == 2);
    if (r.objs.size() == 2) {
      CHECK_STR(r.objs[0], "{\"id\":\"a\",\"s\":\"{[\\\"]}\"}");
      CHECK_STR(r.objs[1], "{\"id\":\"b\",\"nested\":{\"data\":[1,2]}}");
    }
  }
  // "data" inside errors[] text is not mistaken for the key.
  CHECK(runSink("{\"errors\":[{\"description\":\"no data\"}],\"data\":[{\"id\":\"z\"}]}", 1).objs.size() == 1);
  // Empty and missing data[].
  CHECK(runSink("{\"errors\":[],\"data\":[]}", 1).objs.empty());
  CHECK(runSink("{\"errors\":[{\"description\":\"unauthorized user\"}]}", 1).objs.empty());
}

// An object bigger than the buffer is dropped; the ones after it still arrive.
static void testSinkOversizedObjectDropped() {
  std::string big = "{\"id\":\"big\",\"pad\":\"";
  big.append(JsonDataSink::kMaxObj + 100, 'x');
  big += "\"}";
  SinkResult r = runSink("{\"data\":[{\"id\":\"a\"}," + big + ",{\"id\":\"c\"}]}", 512);
  CHECK(r.objects == 2);
  CHECK(r.objs.size() == 2);
  if (r.objs.size() == 2) {
    CHECK_STR(r.objs[0], "{\"id\":\"a\"}");
    CHECK_STR(r.objs[1], "{\"id\":\"c\"}");
  }
}

// A Clip v2 scene whose actions[] alone is bigger than the buffer (issue #14).
static std::string bigScene(const char *id, const char *actionsSep) {
  std::string s = "{\"id\":\"";
  s += id;
  s += "\",\"type\":\"scene\",\"actions\"";
  s += actionsSep;
  s += "[";
  for (int i = 0; i < 200; i++) {
    if (i) {
      s += ",";
    }
    s += "{\"target\":{\"rid\":\"00000000-0000-0000-0000-" + std::to_string(100000000000LL + i) +
         "\",\"rtype\":\"light\"},\"action\":{\"on\":{\"on\":true},\"dimming\":{\"brightness\":80.0},"
         "\"color\":{\"xy\":{\"x\":0.4573,\"y\":0.41}},\"note\":\"}]{[\\\"\"}}";
  }
  s += "],\"metadata\":{\"name\":\"Big party\"},\"group\":{\"rid\":\"room-1\",\"rtype\":\"room\"},"
       "\"status\":{\"active\":\"inactive\"}}";
  return s;
}

static void testSinkSkipsSceneActions() {
  const std::string big = bigScene("s-big", ":");
  CHECK(big.size() > JsonDataSink::kMaxObj);
  const std::string spaced = bigScene("s-spaced", " : ");
  const std::string body = "{\"errors\":[],\"data\":[{\"id\":\"s-small\",\"actions\":[{\"x\":1}]}," + big + "," +
                           spaced + ",{\"id\":\"s-after\",\"actions\":null,\"n\":1}]}";
  for (size_t chunk : {size_t(1), size_t(7), size_t(1024)}) {
    SinkResult r = runSink(body, chunk);
    CHECK(r.objects == 4);
    CHECK(!r.overflow);
    CHECK(r.objs.size() == 4);
    if (r.objs.size() != 4) {
      continue;
    }
    CHECK_STR(r.objs[0], "{\"id\":\"s-small\",\"actions\":[]}");
    const std::string tail =
        "[],\"metadata\":{\"name\":\"Big party\"},\"group\":{\"rid\":\"room-1\",\"rtype\":\"room\"},"
        "\"status\":{\"active\":\"inactive\"}}";
    CHECK_STR(r.objs[1], "{\"id\":\"s-big\",\"type\":\"scene\",\"actions\":" + tail);
    // Whitespace after the colon goes with the skipped value.
    CHECK_STR(r.objs[2], "{\"id\":\"s-spaced\",\"type\":\"scene\",\"actions\" :" + tail);
    CHECK_STR(r.objs[3], "{\"id\":\"s-after\",\"actions\":[],\"n\":1}");

    // What snapshotOnScene reads is still there.
    char out[40];
    CHECK(jsonGetString(r.objs[1].c_str(), "id", out, sizeof(out)));
    CHECK_STR(out, "s-big");
    CHECK(jsonGetObjectString(r.objs[1].c_str(), "metadata", "name", out, sizeof(out)));
    CHECK_STR(out, "Big party");
    CHECK(jsonGetObjectString(r.objs[1].c_str(), "group", "rid", out, sizeof(out)));
    CHECK_STR(out, "room-1");
  }
}

// Only a top-level "actions" key is skipped: nested ones, and the word in a value, stay.
static void testSinkKeepsOtherActions() {
  const std::string obj =
      "{\"id\":\"b1\",\"configuration\":{\"actions\":[1,2]},\"name\":\"\\\"actions\\\":\",\"x\":\"actions\"}";
  SinkResult r = runSink("{\"data\":[" + obj + "]}", 1);
  CHECK(r.objs.size() == 1);
  if (r.objs.size() == 1) {
    CHECK_STR(r.objs[0], obj);
  }
}

// ---------------------------------------------------------------------------
// pages.h / recipes.h: the config poll (console docs/device-api.md)

static const char *kPoll =
    "{\"rev\":7,\"bridgeId\":\"ecb5fa\",\"pageSwipeAxis\":\"vertical\",\"screenTimeoutSec\":5,"
    "\"pages\":["
    "{\"id\":\"p1\",\"name\":\"Cocina \xC3\xB1" "and\xC3\xBA grande\",\"theme\":\"ocean\","
    "\"group\":{\"rtype\":\"room\",\"rid\":\"room-1\",\"groupedLightRid\":\"gl-1\"},"
    "\"dim\":{\"mode\":\"group\",\"rid\":\"gl-1\"}},"
    "{\"id\":\"p2\",\"name\":\"Desk\",\"dim\":{\"mode\":\"lights\",\"rids\":[\"l1\",\"l2\",\"l1\",\"l3\"]}},"
    "{\"id\":\"p3\",\"dim\":null}"
    "],"
    "\"recipes\":["
    "{\"pageId\":\"p1\",\"event\":\"short\",\"action\":\"recall_scene\",\"targets\":["
    "{\"rtype\":\"scene\",\"rid\":\"s1\",\"name\":\"Relajación\"},{\"rid\":\"s2\",\"name\":\"Read\"},"
    "{\"rtype\":\"light\",\"rid\":\"bad\"}]},"
    "{\"pageId\":\"p1\",\"event\":\"double_click\",\"action\":\"toggle\","
    "\"target\":{\"rtype\":\"grouped_light\",\"rid\":\"gl-1\"}},"
    "{\"pageId\":\"p2\",\"event\":\"hold\",\"action\":\"toggle\",\"target\":{\"rtype\":\"light\",\"rid\":\"l1\"}},"
    "{\"pageId\":\"p2\",\"event\":\"short\",\"action\":\"toggle\",\"target\":{\"rtype\":\"scene\",\"rid\":\"s1\"}},"
    "{\"pageId\":\"p2\",\"event\":\"short\",\"action\":\"on\",\"target\":{\"rtype\":\"light\",\"rid\":\"l1\"}}"
    "]}";

static void testConfigPoll() {
  gPageCount = 0;
  gPageIndex = 0;
  gScreenTimeoutSec = kScreenTimeoutDefault;
  uint32_t rev = 0;
  CHECK(recipesParseConfig(kPoll, &rev));
  CHECK(rev == 7);

  CHECK(gPageCount == 3);
  CHECK(gPageSwipeAxis == PAGE_SWIPE_VERTICAL);
  CHECK(gScreenTimeoutSec == kScreenTimeoutMin);  // 5 is clamped up
  CHECK_STR(gPages[0].id, "p1");
  CHECK_STR(gPages[0].name, "Cocina ñand.");  // keeps ñ, clipped to 12 characters
  CHECK_STR(gPages[0].theme, "ocean");
  CHECK_STR(gPages[0].group.rtype, "room");
  CHECK_STR(gPages[0].group.groupedLightRid, "gl-1");
  CHECK(gPages[0].dimMode == PAGE_DIM_GROUP);
  CHECK_STR(gPages[0].dimGroupRid, "gl-1");
  CHECK(gPages[1].dimMode == PAGE_DIM_LIGHTS);
  CHECK(gPages[1].dimLightCount == kMaxDimLights);  // duplicate skipped, capped
  CHECK_STR(gPages[1].dimLights[0], "l1");
  CHECK_STR(gPages[1].dimLights[1], "l2");
  CHECK_STR(gPages[1].theme, "ember");  // default theme
  CHECK_STR(gPages[2].name, "p3");      // no name: the id
  CHECK(gPages[2].dimMode == PAGE_DIM_NONE);

  // Invalid recipes (unknown event, scene target on toggle) are skipped.
  CHECK(gRecipeCount == 3);
  const HueRecipe *tap = recipesFind("p1", "short");
  CHECK(tap != nullptr);
  if (tap) {
    CHECK_STR(tap->action, "recall_scene");
    CHECK(tap->sceneCount == 2);  // the light target is dropped
    CHECK_STR(tap->rid, "s1");
    CHECK_STR(tap->scenes[0].name, "Relajación");
    CHECK_STR(tap->scenes[1].name, "Read");
  }
  const HueRecipe *dbl = recipesFind("p1", "double_click");
  CHECK(dbl != nullptr);
  if (dbl) {
    CHECK_STR(dbl->rtype, "grouped_light");
    CHECK_STR(dbl->rid, "gl-1");
  }
  const HueRecipe *p2 = recipesFind("p2", "short");
  CHECK(p2 != nullptr);
  if (p2) {
    CHECK_STR(p2->action, "on");
  }
  CHECK(recipesFind("p2", "hold") == nullptr);
  CHECK(recipesFindScene("p1") == tap);
}

static void testConfigPollKeepsActivePage() {
  gPageIndex = 1;  // on p2
  uint32_t rev = 0;
  CHECK(recipesParseConfig("{\"rev\":8,\"pages\":[{\"id\":\"p0\"},{\"id\":\"p1\"},{\"id\":\"p2\"}],\"recipes\":[]}",
                           &rev));
  CHECK(rev == 8);
  CHECK(gPageIndex == 2);                          // still p2
  CHECK(gPageSwipeAxis == PAGE_SWIPE_HORIZONTAL);  // no axis: horizontal
  CHECK(gRecipeCount == 0);
  CHECK(gScreenTimeoutSec == kScreenTimeoutMin);  // no timeout: last value kept

  CHECK(recipesParseConfig("{\"rev\":9,\"screenTimeoutSec\":0,\"pages\":[],\"recipes\":[]}", &rev));
  CHECK(gPageCount == 1);  // no pages: the default page
  CHECK_STR(gPages[0].id, "p1");
  CHECK(gPageIndex == 0);
  CHECK(gScreenTimeoutSec == 0);  // 0 = always on

  CHECK(recipesParseConfig("{\"rev\":9,\"screenTimeoutSec\":9000,\"pages\":[],\"recipes\":[]}", &rev));
  CHECK(gScreenTimeoutSec == kScreenTimeoutMax);
}

static void testConfigPollRejects() {
  uint32_t rev = 123;
  CHECK(!recipesParseConfig("{\"upToDate\":true,\"rev\":7}", &rev));
  CHECK(!recipesParseConfig("{\"rev\":-1,\"pages\":[],\"recipes\":[]}", &rev));
  CHECK(!recipesParseConfig("{\"pages\":[],\"recipes\":[]}", &rev));
  CHECK(rev == 123);
}

// What pagesSave / recipesSave write to NVS parses back to the same config.
static void testNvsRoundTrip() {
  uint32_t rev = 0;
  gPageCount = 0;
  gPageIndex = 0;
  CHECK(recipesParseConfig(kPoll, &rev));
  const String pages = pagesToJson();
  const String recipes = recipesToJson();
  const uint8_t pageCount = gPageCount;
  const uint8_t recipeCount = gRecipeCount;

  gPageCount = 0;
  uint8_t n = 0;
  CHECK(pagesParseArray(pages.c_str(), &n));
  CHECK(n == pageCount);
  gPageCount = n;
  CHECK_STR(pagesToJson().c_str(), pages.c_str());

  gRecipeCount = 0;
  n = 0;
  CHECK(recipesParseArray(recipes.c_str(), &n));
  CHECK(n == recipeCount);
  gRecipeCount = n;
  CHECK_STR(recipesToJson().c_str(), recipes.c_str());

  // Through the in-memory NVS stub.
  gRecipeRev = 7;
  CHECK(recipesSave());
  CHECK(pagesSave());
  gRecipeCount = 0;
  gRecipeRev = 0;
  gPageCount = 0;
  recipesLoad();
  pagesLoad();
  CHECK(gRecipeRev == 7);
  CHECK(gRecipeCount == recipeCount);
  CHECK(gPageCount == pageCount);
  CHECK_STR(recipesToJson().c_str(), recipes.c_str());
  CHECK_STR(pagesToJson().c_str(), pages.c_str());
}

// Longest names (two bytes per character) and longest ids still fit NVS: the pages blob in
// one putString, the recipes in their chunks, each under the 4000-byte limit.
static void testNvsLongestNames() {
  static const char *kRid = "0123abcd-4567-89ef-0123-456789abcdef";  // a Hue rid: 36 characters
  gPageCount = kMaxPages;
  gPageIndex = 0;
  for (uint8_t i = 0; i < kMaxPages; i++) {
    Page &p = gPages[i];
    memset(&p, 0, sizeof(p));
    snprintf(p.id, sizeof(p.id), "page-%u-abcdefgh", i);
    circleFoldClip(p.name, sizeof(p.name), "ÑÑÑÑÑÑÑÑÑÑÑÑ", kPageNameMax);
    pageCopyField(p.theme, sizeof(p.theme), "ember");
    pageCopyField(p.group.rtype, sizeof(p.group.rtype), "room");
    pageCopyField(p.group.rid, sizeof(p.group.rid), kRid);
    pageCopyField(p.group.groupedLightRid, sizeof(p.group.groupedLightRid), kRid);
    p.dimMode = PAGE_DIM_LIGHTS;
    p.dimLightCount = kMaxDimLights;
    for (uint8_t j = 0; j < kMaxDimLights; j++) {
      pageCopyField(p.dimLights[j], sizeof(p.dimLights[0]), kRid);
    }
  }
  CHECK(utf8Chars(gPages[0].name) == 12);
  const String pages = pagesToJson();
  CHECK(pages.length() < 4000);
  CHECK(pagesSave());
  gPageCount = 0;
  pagesLoad();
  CHECK(gPageCount == kMaxPages);
  CHECK_STR(gPages[5].name, "ÑÑÑÑÑÑÑÑÑÑÑÑ");
  CHECK_STR(pagesToJson().c_str(), pages.c_str());

  // Every page with a tap and a double tap, each cycling the most scenes.
  static const char *kEvents[] = {"short", "double_click"};
  gRecipeCount = 0;
  for (uint8_t i = 0; i < kMaxPages; i++) {
    for (const char *ev : kEvents) {
      HueRecipe &r = gRecipes[gRecipeCount++];
      memset(&r, 0, sizeof(r));
      recipeCopyField(r.pageId, sizeof(r.pageId), gPages[i].id);
      recipeCopyField(r.event, sizeof(r.event), ev);
      recipeCopyField(r.action, sizeof(r.action), "recall_scene");
      recipeCopyField(r.rtype, sizeof(r.rtype), "scene");
      recipeCopyField(r.rid, sizeof(r.rid), kRid);
      r.sceneCount = kMaxScenes;
      for (uint8_t s = 0; s < kMaxScenes; s++) {
        recipeCopyField(r.scenes[s].rid, sizeof(r.scenes[0].rid), kRid);
        circleFoldClip(r.scenes[s].name, sizeof(r.scenes[0].name), "üüüüüüüüüüüüüüüüüüüüüüüü",
                       kSceneNameMax);
      }
    }
  }
  CHECK(utf8Chars(gRecipes[0].scenes[0].name) == 24);
  const String recipes = recipesToJson();
  gRecipeRev = 11;
  CHECK(recipesSave());
  for (const auto &kv : Preferences::store()["recipes"]) {
    CHECK(kv.second.size() < 4000);
  }
  gRecipeCount = 0;
  recipesLoad();
  CHECK(gRecipeCount == 2 * kMaxPages);
  CHECK_STR(recipesToJson().c_str(), recipes.c_str());
}

int main() {
  testGetString();
  testObjectString();
  testGetInt();
  testHueState();
  testFindRidByRtype();
  testCircleFold();
  testCircleFoldClip();
  testCircleToCp437();
  testAppendEscaped();
  testEachArray();
  testEachTopLevel();
  testSinkBasic();
  testSinkOversizedObjectDropped();
  testSinkSkipsSceneActions();
  testSinkKeepsOtherActions();
  testConfigPoll();
  testConfigPollKeepsActivePage();
  testConfigPollRejects();
  testNvsRoundTrip();
  testNvsLongestNames();
  if (gFailures) {
    fprintf(stderr, "%d of %d checks failed\n", gFailures, gChecks);
    return 1;
  }
  printf("host tests: %d checks passed\n", gChecks);
  return 0;
}
