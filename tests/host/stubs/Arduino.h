#pragma once

// Minimal Arduino stand-ins so the firmware's plain-C++ headers compile on a PC.
// Only what json_util.h, pages.h and recipes.h use. Not a general Arduino emulation.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <string>

class String {
 public:
  String() {}
  String(const char *s) : s_(s ? s : "") {}
  String(const std::string &s) : s_(s) {}

  const char *c_str() const { return s_.c_str(); }
  unsigned int length() const { return static_cast<unsigned int>(s_.size()); }

  String &operator+=(const String &o) {
    s_ += o.s_;
    return *this;
  }
  String &operator+=(const char *o) {
    s_ += o ? o : "";
    return *this;
  }
  String &operator+=(char c) {
    s_ += c;
    return *this;
  }

  bool equals(const String &o) const { return s_ == o.s_; }
  bool equals(const char *o) const { return s_ == (o ? o : ""); }
  bool equalsIgnoreCase(const String &o) const { return strcasecmp(s_.c_str(), o.s_.c_str()) == 0; }
  bool operator==(const String &o) const { return s_ == o.s_; }
  bool operator==(const char *o) const { return s_ == (o ? o : ""); }
  bool operator!=(const String &o) const { return s_ != o.s_; }
  bool operator!=(const char *o) const { return !(*this == o); }

 private:
  std::string s_;
};

// Tests set this to drive code that reads the clock.
inline unsigned long gHostMillis = 0;
inline unsigned long millis() { return gHostMillis; }
