#include "Logger.h"

#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "nav/nav_serial_json.h"

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
    return true;
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
    gLoggerStartMs = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
  }
}

void writeLine(const char *line) {
  if (line == nullptr) {
    return;
  }
  if (gLoggerMutex != nullptr) {
    xSemaphoreTake(gLoggerMutex, portMAX_DELAY);
  }

  fwrite(line, 1, strlen(line), stdout);
  fwrite("\r\n", 1, 2, stdout);
  fflush(stdout);

  if (gLoggerMutex != nullptr) {
    xSemaphoreGive(gLoggerMutex);
  }
}

void rawf(const char *level, const char *tag, const char *format, va_list args) {
  if (!shouldPrintLevel(level)) {
    return;
  }

  char message[224];
  vsnprintf(message, sizeof(message), format, args);

  const uint32_t elapsedMs = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL) - gLoggerStartMs;
  char text[320];
  const int textWritten = snprintf(text,
                                   sizeof(text),
                                   "t=%lums [%s] [%s] %s",
                                   static_cast<unsigned long>(elapsedMs),
                                   level,
                                   tag,
                                   message);
  if (textWritten >= 0) {
    size_t textLength = static_cast<size_t>(textWritten);
    if (textLength >= sizeof(text)) {
      textLength = sizeof(text) - 1u;
      text[textLength] = '\0';
    }
  }

  char line[1024];
  const int written = nav_serial_write_log_record(line, sizeof(line), elapsedMs, level, tag, textWritten >= 0 ? text : message);
  if (written >= 0) {
    if (static_cast<size_t>(written) >= sizeof(line)) {
      snprintf(line,
               sizeof(line),
               "{\"type\":\"log\",\"ts\":%lu,\"level\":\"WARN\",\"tag\":\"LOGGER\",\"text\":\"log record too long\"}",
               static_cast<unsigned long>(elapsedMs));
    }

    writeLine(line);
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
