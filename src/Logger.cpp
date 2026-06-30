#include "Logger.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifndef LOG_LEVEL
#define LOG_LEVEL 2
#endif

namespace {
SemaphoreHandle_t gLoggerMutex = nullptr;
uint32_t gLoggerStartMs = 0;

uint8_t configuredLogLevel() {
  if (LOG_LEVEL < 0) {
    return 0;
  }
  if (LOG_LEVEL > 4) {
    return 4;
  }
  return static_cast<uint8_t>(LOG_LEVEL);
}

bool shouldPrintLevel(const char *level) {
  if (strcmp(level, "ERROR") == 0 || strcmp(level, "FAIL") == 0) {
    return configuredLogLevel() >= 0;
  }
  if (strcmp(level, "WARN") == 0) {
    return configuredLogLevel() >= 1;
  }
  if (strcmp(level, "INFO") == 0 || strcmp(level, "OK") == 0) {
    return configuredLogLevel() >= 2;
  }
  if (strcmp(level, "DEBUG") == 0) {
    return configuredLogLevel() >= 3;
  }
  if (strcmp(level, "TRACE") == 0) {
    return configuredLogLevel() >= 4;
  }
  return true;
}

const char *levelName(LogLevel level) {
  switch (level) {
    case LogLevel::Error:
      return "ERROR";
    case LogLevel::Warn:
      return "WARN";
    case LogLevel::Info:
      return "INFO";
    case LogLevel::Debug:
      return "DEBUG";
    case LogLevel::Trace:
      return "TRACE";
  }
  return "INFO";
}
}  // namespace

namespace Logger {
void begin() {
  if (gLoggerMutex == nullptr) {
    gLoggerMutex = xSemaphoreCreateMutex();
    gLoggerStartMs = millis();
  }
}

void rawf(const char *level, const char *tag, const char *format, va_list args) {
  if (!shouldPrintLevel(level)) {
    return;
  }

  char message[224];
  vsnprintf(message, sizeof(message), format, args);

  if (gLoggerMutex != nullptr) {
    xSemaphoreTake(gLoggerMutex, portMAX_DELAY);
  }

  const uint32_t elapsedMs = millis() - gLoggerStartMs;
  char line[320];
  const int written = snprintf(line,
                               sizeof(line),
                               "t=%lums [%s] [%s] %s\r\n",
                               static_cast<unsigned long>(elapsedMs),
                               level,
                               tag,
                               message);
  if (written >= 0) {
    size_t lineLength = static_cast<size_t>(written);
    if (lineLength >= sizeof(line)) {
      lineLength = sizeof(line) - 1;
      if (lineLength >= 2) {
        line[lineLength - 2] = '\r';
        line[lineLength - 1] = '\n';
      }
    }

    Serial.write(reinterpret_cast<const uint8_t *>(line), lineLength);
    Serial.flush();
  }

  if (gLoggerMutex != nullptr) {
    xSemaphoreGive(gLoggerMutex);
  }
}

void logf(LogLevel level, const char *tag, const char *format, ...) {
  va_list args;
  va_start(args, format);
  rawf(levelName(level), tag, format, args);
  va_end(args);
}

void okf(const char *tag, const char *format, ...) {
  va_list args;
  va_start(args, format);
  rawf("OK", tag, format, args);
  va_end(args);
}

void failf(const char *tag, const char *format, ...) {
  va_list args;
  va_start(args, format);
  rawf("FAIL", tag, format, args);
  va_end(args);
}
}  // namespace Logger
