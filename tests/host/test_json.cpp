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

static void testAsciiFold() {
  char out[32];
  asciiFold(out, sizeof(out), "Cocina \xC3\xB1" "and\xC3\xBA");  // "Cocina ñandú"
  CHECK_STR(out, "Cocina nandu");
  // Latin Extended-A (lead bytes 0xC4 / 0xC5) folds to the base letter.
  asciiFold(out, sizeof(out), "\xC5\x81" "azienka");  // "Łazienka"
  CHECK_STR(out, "Lazienka");
  asciiFold(out, sizeof(out), "Kuchyn\xC4\x9B");  // "Kuchyně"
  CHECK_STR(out, "Kuchyne");
  asciiFold(out, sizeof(out), "I\xC5\x9F\xC4\xB1k");  // "Işık"
  CHECK_STR(out, "Isik");
  asciiFold(out, sizeof(out), "\xC4\x80\xC4\x8D\xC5\x91\xC5\xBE\xC5\xBF");  // "Āčőžſ": both ends
  CHECK_STR(out, "Acozs");
  asciiFold(out, sizeof(out), "\xC4\x90ur\xC4\x91" "a \xC5\x92uvre");  // "Đurđa Œuvre"
  CHECK_STR(out, "Durda Ouvre");
  // Other two-byte letters (Greek here) are still dropped.
  asciiFold(out, sizeof(out), "a\xCE\xB1" "b");
  CHECK_STR(out, "ab");
  asciiFold(out, sizeof(out), "Sala \xF0\x9F\x92\xA1!");  // emoji dropped
  CHECK_STR(out, "Sala !");
  asciiFoldClip(out, sizeof(out), "Living room lamps", 12);
  CHECK_STR(out, "Living room.");
  asciiFoldClip(out, sizeof(out), "Short", 12);
  CHECK_STR(out, "Short");
}

static void testAppendEscaped() {
  String s;
  jsonAppendEscaped(s, "a\"b\\c\nd\x01");
  CHECK_STR(s.c_str(), "\"a\\\"b\\\\c\\nd\"");
  String n;
  jsonAppendEscaped(n, nullptr);
  CHECK_STR(n.c_str(), "\"\"");
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
    "{\"rtype\":\"scene\",\"rid\":\"s1\",\"name\":\"Relax\"},{\"rid\":\"s2\",\"name\":\"Read\"},"
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
  CHECK_STR(gPages[0].name, "Cocina nand.");  // folded to ASCII, clipped to 12 characters
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

int main() {
  testGetString();
  testObjectString();
  testGetInt();
  testHueState();
  testFindRidByRtype();
  testAsciiFold();
  testAppendEscaped();
  testEachArray();
  testSinkBasic();
  testSinkOversizedObjectDropped();
  testSinkSkipsSceneActions();
  testSinkKeepsOtherActions();
  testConfigPoll();
  testConfigPollKeepsActivePage();
  testConfigPollRejects();
  testNvsRoundTrip();
  if (gFailures) {
    fprintf(stderr, "%d of %d checks failed\n", gFailures, gChecks);
    return 1;
  }
  printf("host tests: %d checks passed\n", gChecks);
  return 0;
}
