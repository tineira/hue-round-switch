#pragma once

#include <stddef.h>
#include <stdint.h>

// Just the virtuals JsonDataSink overrides (Print + Stream in the real core).
class Stream {
 public:
  virtual ~Stream() {}
  virtual size_t write(uint8_t c) = 0;
  virtual size_t write(const uint8_t *data, size_t size) {
    size_t n = 0;
    for (size_t i = 0; i < size; i++) {
      n += write(data[i]);
    }
    return n;
  }
  virtual int available() = 0;
  virtual int read() = 0;
  virtual int peek() = 0;
  virtual void flush() {}
};
