#pragma once

#include <Arduino.h>
#include <stdarg.h>

enum class LogLevel : uint8_t {
  Error = 0,
  Warn = 1,
  Info = 2,
  Debug = 3,
  Trace = 4,
};

namespace Logger {
void begin();
void logf(LogLevel level, const char *tag, const char *format, ...);
void okf(const char *tag, const char *format, ...);
void failf(const char *tag, const char *format, ...);
void rawf(const char *level, const char *tag, const char *format, va_list args);

inline void errorf(const char *tag, const char *format, ...) {
  va_list args;
  va_start(args, format);
  rawf("ERROR", tag, format, args);
  va_end(args);
}

inline void warnf(const char *tag, const char *format, ...) {
  va_list args;
  va_start(args, format);
  rawf("WARN", tag, format, args);
  va_end(args);
}

inline void infof(const char *tag, const char *format, ...) {
  va_list args;
  va_start(args, format);
  rawf("INFO", tag, format, args);
  va_end(args);
}

inline void debugf(const char *tag, const char *format, ...) {
  va_list args;
  va_start(args, format);
  rawf("DEBUG", tag, format, args);
  va_end(args);
}

inline void tracef(const char *tag, const char *format, ...) {
  va_list args;
  va_start(args, format);
  rawf("TRACE", tag, format, args);
  va_end(args);
}
}  // namespace Logger
