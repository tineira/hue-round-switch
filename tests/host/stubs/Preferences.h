#pragma once

#include <map>
#include <string>
#include "Arduino.h"

// In-memory NVS: namespace -> key -> value, shared by every Preferences object like the real one.
class Preferences {
 public:
  static std::map<std::string, std::map<std::string, std::string>> &store() {
    static std::map<std::string, std::map<std::string, std::string>> s;
    return s;
  }

  bool begin(const char *name, bool readOnly = false) {
    ns_ = name ? name : "";
    ro_ = readOnly;
    open_ = true;
    return true;
  }
  void end() { open_ = false; }
  bool clear() {
    if (!open_ || ro_) {
      return false;
    }
    store()[ns_].clear();
    return true;
  }

  size_t putString(const char *key, const char *value) {
    if (!open_ || ro_ || !value) {
      return 0;
    }
    store()[ns_][key] = value;
    return strlen(value);
  }
  size_t putString(const char *key, const String &value) { return putString(key, value.c_str()); }
  String getString(const char *key, const String &def = String()) {
    const std::string *v = find(key);
    return v ? String(v->c_str()) : def;
  }

  size_t putUChar(const char *key, uint8_t v) { return putNum(key, v, 1); }
  size_t putUShort(const char *key, uint16_t v) { return putNum(key, v, 2); }
  size_t putUInt(const char *key, uint32_t v) { return putNum(key, v, 4); }
  uint8_t getUChar(const char *key, uint8_t def = 0) { return static_cast<uint8_t>(getNum(key, def)); }
  uint16_t getUShort(const char *key, uint16_t def = 0) { return static_cast<uint16_t>(getNum(key, def)); }
  uint32_t getUInt(const char *key, uint32_t def = 0) { return static_cast<uint32_t>(getNum(key, def)); }

 private:
  std::string ns_;
  bool ro_ = false;
  bool open_ = false;

  const std::string *find(const char *key) {
    auto n = store().find(ns_);
    if (n == store().end()) {
      return nullptr;
    }
    auto k = n->second.find(key);
    return k == n->second.end() ? nullptr : &k->second;
  }
  size_t putNum(const char *key, unsigned long v, size_t width) {
    if (!open_ || ro_) {
      return 0;
    }
    store()[ns_][key] = std::to_string(v);
    return width;
  }
  unsigned long getNum(const char *key, unsigned long def) {
    const std::string *v = find(key);
    return v ? strtoul(v->c_str(), nullptr, 10) : def;
  }
};
