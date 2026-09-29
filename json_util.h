#pragma once

#include <Arduino.h>
#include <Stream.h>
#include <string.h>
#include <stdlib.h>

// Extracts fields from compact JSON (Clip v2 / console). Not a full parser.

inline void jsonAppendEscaped(String &out, const char *s) {
  out += '"';
  if (!s) {
    out += '"';
    return;
  }
  for (; *s; s++) {
    const char c = *s;
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if (c == '\n') {
      out += "\\n";
    } else if (c == '\r') {
      out += "\\r";
    } else if (c == '\t') {
      out += "\\t";
    } else if (static_cast<uint8_t>(c) < 0x20) {
      continue;
    } else {
      out += c;
    }
  }
  out += '"';
}

inline bool jsonGetString(const char *json, const char *key, char *out, size_t outSz) {
  if (!json || !key || !out || outSz < 2) {
    return false;
  }
  char needle[48];
  snprintf(needle, sizeof(needle), "\"%s\":\"", key);
  const char *p = strstr(json, needle);
  if (!p) {
    return false;
  }
  p += strlen(needle);
  size_t i = 0;
  while (*p && *p != '"' && i + 1 < outSz) {
    if (*p == '\\' && p[1]) {
      p++;
      char c = *p++;
      if (c == 'n') {
        c = '\n';
      } else if (c == 't') {
        c = '\t';
      } else if (c == 'r') {
        c = '\r';
      }
      out[i++] = c;
    } else {
      out[i++] = *p++;
    }
  }
  out[i] = 0;
  return true;
}

inline bool jsonGetObjectString(const char *json, const char *objKey, const char *field, char *out,
                                size_t outSz) {
  if (!json || !objKey) {
    return false;
  }
  char needle[48];
  snprintf(needle, sizeof(needle), "\"%s\":", objKey);
  const char *p = strstr(json, needle);
  if (!p) {
    return false;
  }
  p += strlen(needle);
  while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') {
    p++;
  }
  if (*p != '{') {
    return false;
  }
  return jsonGetString(p, field, out, outSz);
}

inline bool jsonHasKey(const char *json, const char *key) {
  if (!json || !key) {
    return false;
  }
  char needle[48];
  snprintf(needle, sizeof(needle), "\"%s\":", key);
  return strstr(json, needle) != nullptr;
}

// Pointer to the object's '{', or nullptr if missing / null.
inline const char *jsonObjectPtr(const char *json, const char *objKey) {
  if (!json || !objKey) {
    return nullptr;
  }
  char needle[48];
  snprintf(needle, sizeof(needle), "\"%s\":", objKey);
  const char *p = strstr(json, needle);
  if (!p) {
    return nullptr;
  }
  p += strlen(needle);
  while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') {
    p++;
  }
  if (*p != '{') {
    return nullptr;
  }
  return p;
}

inline int jsonGetInt(const char *json, const char *key, int defVal) {
  if (!json || !key) {
    return defVal;
  }
  char needle[48];
  snprintf(needle, sizeof(needle), "\"%s\":", key);
  const char *p = strstr(json, needle);
  if (!p) {
    return defVal;
  }
  p += strlen(needle);
  while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') {
    p++;
  }
  if (*p != '-' && (*p < '0' || *p > '9')) {
    return defVal;
  }
  return atoi(p);
}

inline bool jsonHueOn(const char *json, bool *on) {
  if (!json || !on) {
    return false;
  }
  const char *p = strstr(json, "\"on\"");
  while (p) {
    const char *q = p + 4;
    while (*q == ' ') {
      q++;
    }
    if (*q != ':') {
      p = strstr(p + 4, "\"on\"");
      continue;
    }
    q++;
    while (*q == ' ') {
      q++;
    }
    if (*q == '{') {
      const char *close = strchr(q, '}');
      const char *inner = strstr(q, "\"on\"");
      if (inner && (!close || inner < close)) {
        inner += 4;
        while (*inner == ' ') {
          inner++;
        }
        if (*inner == ':') {
          inner++;
          while (*inner == ' ') {
            inner++;
          }
          if (strncmp(inner, "true", 4) == 0) {
            *on = true;
            return true;
          }
          if (strncmp(inner, "false", 5) == 0) {
            *on = false;
            return true;
          }
        }
      }
    }
    p = strstr(p + 4, "\"on\"");
  }
  return false;
}

// Clip v2 scene: status.active is inactive | static | dynamic_palette.
inline bool jsonHueSceneActive(const char *json, bool *active) {
  if (!json || !active) {
    return false;
  }
  const char *st = strstr(json, "\"status\"");
  if (!st) {
    return false;
  }
  const char *a = strstr(st, "\"active\"");
  if (!a) {
    return false;
  }
  a = strchr(a + 8, ':');
  if (!a) {
    return false;
  }
  a++;
  while (*a == ' ' || *a == '\t' || *a == '\n' || *a == '\r') {
    a++;
  }
  if (*a != '"') {
    return false;
  }
  a++;
  *active = strncmp(a, "inactive", 8) != 0;
  return true;
}

// 5×7 font: letters with accents fold to their base letter (ñ→n, Ł→L); other non-ASCII is
// dropped. The circle does not paint UTF-8. Covers Latin-1 (lead byte 0xC3) and Latin
// Extended-A (U+0100–U+017F, lead bytes 0xC4 and 0xC5).
inline void asciiFold(char *dst, size_t dstSz, const char *src) {
  if (!dst || dstSz == 0) {
    return;
  }
  dst[0] = 0;
  if (!src) {
    return;
  }
  static const char kC3[64] = {
      'A', 'A', 'A', 'A', 'A', 'A', 'A', 'C', 'E', 'E', 'E', 'E', 'I', 'I', 'I', 'I',
      'D', 'N', 'O', 'O', 'O', 'O', 'O', 'x', 'O', 'U', 'U', 'U', 'U', 'Y', 'T', 's',
      'a', 'a', 'a', 'a', 'a', 'a', 'a', 'c', 'e', 'e', 'e', 'e', 'i', 'i', 'i', 'i',
      'd', 'n', 'o', 'o', 'o', 'o', 'o', 'x', 'o', 'u', 'u', 'u', 'u', 'y', 't', 'y'};
  // U+0100–U+017F. Letters without a decomposition take the closest base letter
  // (Đ→D, Ħ→H, ı→i, Ł→L, Ŋ→N, Ŧ→T, ſ→s; the ligatures Ĳ and Œ keep their first letter).
  static const char kC4C5[128] = {
      'A', 'a', 'A', 'a', 'A', 'a', 'C', 'c', 'C', 'c', 'C', 'c', 'C', 'c', 'D', 'd',
      'D', 'd', 'E', 'e', 'E', 'e', 'E', 'e', 'E', 'e', 'E', 'e', 'G', 'g', 'G', 'g',
      'G', 'g', 'G', 'g', 'H', 'h', 'H', 'h', 'I', 'i', 'I', 'i', 'I', 'i', 'I', 'i',
      'I', 'i', 'I', 'i', 'J', 'j', 'K', 'k', 'k', 'L', 'l', 'L', 'l', 'L', 'l', 'L',
      'l', 'L', 'l', 'N', 'n', 'N', 'n', 'N', 'n', 'n', 'N', 'n', 'O', 'o', 'O', 'o',
      'O', 'o', 'O', 'o', 'R', 'r', 'R', 'r', 'R', 'r', 'S', 's', 'S', 's', 'S', 's',
      'S', 's', 'T', 't', 'T', 't', 'T', 't', 'U', 'u', 'U', 'u', 'U', 'u', 'U', 'u',
      'U', 'u', 'U', 'u', 'W', 'w', 'Y', 'y', 'Y', 'Z', 'z', 'Z', 'z', 'Z', 'z', 's'};
  size_t o = 0;
  const uint8_t *p = reinterpret_cast<const uint8_t *>(src);
  while (*p && o + 1 < dstSz) {
    const uint8_t c = *p++;
    if (c < 0x80) {
      if (c >= 32 && c != 127) {
        dst[o++] = static_cast<char>(c);
      }
      continue;
    }
    if ((c & 0xE0) == 0xC0 && *p) {
      const uint8_t c2 = *p++;
      if (c2 >= 0x80 && c2 < 0xC0) {
        if (c == 0xC3) {
          dst[o++] = kC3[c2 - 0x80];
        } else if (c == 0xC4 || c == 0xC5) {
          dst[o++] = kC4C5[(c - 0xC4) * 64 + (c2 - 0x80)];
        }
      }
      continue;
    }
    if ((c & 0xF0) == 0xE0 && p[0] && p[1]) {
      p += 2;
      continue;
    }
    if ((c & 0xF8) == 0xF0 && p[0] && p[1] && p[2]) {
      p += 3;
      continue;
    }
  }
  dst[o] = 0;
}

inline void asciiFoldClip(char *dst, size_t dstSz, const char *src, uint8_t maxChars) {
  char fold[96];
  asciiFold(fold, sizeof(fold), src);
  if (!dst || dstSz == 0) {
    return;
  }
  if (maxChars + 1 < dstSz) {
    dstSz = static_cast<size_t>(maxChars) + 1;
  }
  const size_t n = strlen(fold);
  if (n + 1 <= dstSz) {
    memcpy(dst, fold, n + 1);
    return;
  }
  if (dstSz < 2) {
    dst[0] = 0;
    return;
  }
  size_t keep = dstSz - 2;
  memcpy(dst, fold, keep);
  dst[keep] = '.';
  dst[keep + 1] = 0;
}

inline bool jsonHueBrightness(const char *json, int *pct) {
  if (!json || !pct) {
    return false;
  }
  const char *block = strstr(json, "\"dimming\"");
  if (!block) {
    return false;
  }
  const char *p = strstr(block, "\"brightness\"");
  if (!p) {
    return false;
  }
  p = strchr(p, ':');
  if (!p) {
    return false;
  }
  p++;
  while (*p == ' ' || *p == '\n' || *p == '\t') {
    p++;
  }
  if (*p != '-' && (*p < '0' || *p > '9')) {
    return false;
  }
  int v = (int)(atof(p) + 0.5f);
  if (v < 0) {
    v = 0;
  }
  if (v > 100) {
    v = 100;
  }
  *pct = v;
  return true;
}

// Rid whose paired rtype matches (e.g. grouped_light in services[]).
inline bool jsonFindRidByRtype(const char *json, const char *rtype, char *out, size_t outSz) {
  if (!json || !rtype || !out || outSz < 2) {
    return false;
  }
  char needle[64];
  snprintf(needle, sizeof(needle), "\"rtype\":\"%s\"", rtype);
  const char *hit = strstr(json, needle);
  if (!hit) {
    snprintf(needle, sizeof(needle), "\"rtype\": \"%s\"", rtype);
    hit = strstr(json, needle);
  }
  if (!hit) {
    return false;
  }
  const char *start = json;
  if (hit - json > 120) {
    start = hit - 120;
  }
  const char *ridKey = nullptr;
  for (const char *q = start; q < hit; q++) {
    if (strncmp(q, "\"rid\":\"", 7) == 0) {
      ridKey = q;
    }
  }
  if (!ridKey) {
    return false;
  }
  ridKey += 7;
  size_t i = 0;
  while (ridKey[i] && ridKey[i] != '"' && i + 1 < outSz) {
    out[i] = ridKey[i];
    i++;
  }
  out[i] = 0;
  return i > 0;
}

inline bool jsonIsSpace(char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }

typedef void (*JsonObjFn)(const char *obj, void *ctx);
typedef void (*JsonStrFn)(const char *s, void *ctx);

// Walks "key":["a","b"] (strings, not objects).
inline void jsonEachArrayString(const char *json, const char *key, JsonStrFn fn, void *ctx) {
  if (!json || !key || !fn) {
    return;
  }
  char needle[48];
  snprintf(needle, sizeof(needle), "\"%s\":", key);
  const char *p = strstr(json, needle);
  if (!p) {
    return;
  }
  p = strchr(p, '[');
  if (!p) {
    return;
  }
  p++;
  bool inString = false;
  bool escape = false;
  int depth = 0;
  char buf[40];
  size_t n = 0;
  for (; *p; p++) {
    const char c = *p;
    if (!inString && depth == 0 && c == ']') {
      return;
    }
    if (escape) {
      if (inString && n + 1 < sizeof(buf)) {
        buf[n++] = c;
      }
      escape = false;
      continue;
    }
    if (inString) {
      if (c == '\\') {
        escape = true;
        continue;
      }
      if (c == '"') {
        inString = false;
        buf[n] = 0;
        if (depth == 0) {
          fn(buf, ctx);
        }
        n = 0;
        continue;
      }
      if (n + 1 < sizeof(buf)) {
        buf[n++] = c;
      }
      continue;
    }
    if (c == '"') {
      inString = true;
      n = 0;
      continue;
    }
    if (c == '{') {
      depth++;
    } else if (c == '}' && depth > 0) {
      depth--;
    }
  }
}

// Walks the objects of an array whose '[' is just before p, up to the matching ']' (or the
// end of the string). Each object, nested objects included, is copied and handed to fn.
// Braces and escaped quotes inside strings do not count. Values that are not objects
// (strings, numbers) at the array's top level are skipped.
inline void jsonEachObjectAfterBracket(const char *p, JsonObjFn fn, void *ctx) {
  if (!p || !fn) {
    return;
  }
  const char *start = nullptr;
  int depth = 0;
  bool inString = false;
  bool escape = false;
  for (; *p; p++) {
    const char c = *p;
    if (depth == 0 && !inString) {
      if (c == ']') {
        return;
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
        const size_t n = static_cast<size_t>(p - start + 1);
        char *tmp = static_cast<char *>(malloc(n + 1));
        if (tmp) {
          memcpy(tmp, start, n);
          tmp[n] = 0;
          fn(tmp, ctx);
          free(tmp);
        }
        start = nullptr;
      }
    }
  }
}

// Walks "key":[{...},{...}].
inline void jsonEachArrayObject(const char *json, const char *key, JsonObjFn fn, void *ctx) {
  if (!json || !key || !fn) {
    return;
  }
  char needle[48];
  snprintf(needle, sizeof(needle), "\"%s\":", key);
  const char *p = strstr(json, needle);
  if (!p) {
    return;
  }
  p = strchr(p, '[');
  if (!p) {
    return;
  }
  jsonEachObjectAfterBracket(p + 1, fn, ctx);
}

// Walks a top-level array, [{...},{...}] (the form pages and recipes are stored in NVS).
// Does nothing unless json[0] is '['.
inline void jsonEachTopLevelObject(const char *json, JsonObjFn fn, void *ctx) {
  if (!json || json[0] != '[') {
    return;
  }
  jsonEachObjectAfterBracket(json + 1, fn, ctx);
}

inline bool jsonStringField(const String &body, const char *key, String *out) {
  char buf[96];
  if (!jsonGetString(body.c_str(), key, buf, sizeof(buf)) || !buf[0]) {
    return false;
  }
  *out = buf;
  return true;
}

// Receives the HTTP body (chunked already decoded) and hands over each object of data[].
// A top-level "actions" value is not copied: it arrives as "actions":[]. Clip v2 scenes list
// one action per light there, which can run past kMaxObj, and the snapshot never reads it.
class JsonDataSink : public Stream {
 public:
  static const size_t kMaxObj = 20480;

  JsonObjFn onObject = nullptr;
  void *ctx = nullptr;
  int objects = 0;
  bool overflow = false;

  JsonDataSink() {
    buf_ = static_cast<char *>(malloc(kMaxObj));
  }

  ~JsonDataSink() {
    free(buf_);
  }

  size_t write(uint8_t c) override {
    feed(static_cast<char>(c));
    return 1;
  }

  size_t write(const uint8_t *data, size_t size) override {
    for (size_t i = 0; i < size; i++) {
      feed(static_cast<char>(data[i]));
    }
    return size;
  }

  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}

 private:
  enum State { kSeekData, kSeekColon, kSeekArray, kScan, kObject, kDone };

  char *buf_ = nullptr;
  size_t len_ = 0;
  int depth_ = 0;
  bool inString_ = false;
  bool escape_ = false;
  State state_ = kSeekData;
  uint8_t match_ = 0;
  // Skipping the value of a top-level "actions" key (see the class comment).
  bool skipping_ = false;
  bool skipStarted_ = false;
  bool skipInString_ = false;
  bool skipEscape_ = false;
  int skipNest_ = 0;

  void startSkip() {
    skipping_ = true;
    skipStarted_ = false;
    skipInString_ = false;
    skipEscape_ = false;
    skipNest_ = 0;
  }

  void finishSkip() {
    skipping_ = false;
    if (buf_ && len_ + 2 < kMaxObj) {
      buf_[len_++] = '[';
      buf_[len_++] = ']';
    } else {
      overflow = true;
    }
  }

  // After a ':' at depth 1: is the key just before it "actions"?
  bool keyIsActions() const {
    static const char kKey[] = "\"actions\"";
    static const size_t kKeyLen = sizeof(kKey) - 1;
    size_t end = len_ - 1;  // the ':'
    while (end > 0 && jsonIsSpace(buf_[end - 1])) {
      end--;
    }
    return end >= kKeyLen + 1 && memcmp(buf_ + end - kKeyLen, kKey, kKeyLen) == 0;
  }

  // true: the byte belongs to the skipped value. false: the value ended just before this
  // byte (a bare number / true / false / null), so the object parser takes it.
  bool feedSkip(char c) {
    if (!skipStarted_) {
      if (jsonIsSpace(c)) {
        return true;
      }
      skipStarted_ = true;
      if (c == '[' || c == '{') {
        skipNest_ = 1;
      } else if (c == '"') {
        skipInString_ = true;
      }
      return true;
    }
    if (skipInString_) {
      if (skipEscape_) {
        skipEscape_ = false;
      } else if (c == '\\') {
        skipEscape_ = true;
      } else if (c == '"') {
        skipInString_ = false;
        if (skipNest_ == 0) {
          finishSkip();
        }
      }
      return true;
    }
    if (skipNest_ > 0) {
      if (c == '"') {
        skipInString_ = true;
      } else if (c == '[' || c == '{') {
        skipNest_++;
      } else if (c == ']' || c == '}') {
        if (--skipNest_ == 0) {
          finishSkip();
        }
      }
      return true;
    }
    if (c == ',' || c == '}' || c == ']') {
      finishSkip();
      return false;
    }
    return true;
  }

  void feed(char c) {
    switch (state_) {
      case kSeekData: {
        static const char kKey[] = "\"data\"";
        if (c == kKey[match_]) {
          match_++;
          if (kKey[match_] == 0) {
            state_ = kSeekColon;
            match_ = 0;
          }
        } else {
          match_ = (c == kKey[0]) ? 1 : 0;
        }
        break;
      }
      case kSeekColon:
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
          break;
        }
        if (c == ':') {
          state_ = kSeekArray;
        } else {
          state_ = kSeekData;
        }
        break;
      case kSeekArray:
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
          break;
        }
        if (c == '[') {
          state_ = kScan;
        } else {
          state_ = kSeekData;
        }
        break;
      case kScan:
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == ',') {
          break;
        }
        if (c == ']') {
          state_ = kDone;
          break;
        }
        if (c == '{') {
          if (!buf_) {
            overflow = true;
            break;
          }
          state_ = kObject;
          depth_ = 1;
          len_ = 1;
          buf_[0] = '{';
          inString_ = false;
          escape_ = false;
          skipping_ = false;
        }
        break;
      case kObject: {
        if (skipping_ && feedSkip(c)) {
          break;
        }
        if (buf_ && len_ + 1 < kMaxObj) {
          buf_[len_++] = c;
        } else {
          overflow = true;
        }
        if (escape_) {
          escape_ = false;
          break;
        }
        if (inString_) {
          if (c == '\\') {
            escape_ = true;
          } else if (c == '"') {
            inString_ = false;
          }
          break;
        }
        if (c == '"') {
          inString_ = true;
          break;
        }
        if (c == ':' && depth_ == 1 && !overflow && keyIsActions()) {
          startSkip();
          break;
        }
        if (c == '{') {
          depth_++;
        } else if (c == '}') {
          depth_--;
          if (depth_ == 0) {
            if (buf_ && !overflow && onObject) {
              buf_[len_] = 0;
              onObject(buf_, ctx);
              objects++;
            }
            overflow = false;
            state_ = kScan;
            len_ = 0;
          }
        }
        break;
      }
      case kDone:
        break;
    }
  }
};
