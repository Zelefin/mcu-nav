#include "BoardPins.h"
#include "HealthStatus.h"
#include "Logger.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
static constexpr uart_port_t kGpsUart = UART_NUM_2;
static constexpr uint32_t kGpsBaud = 115200;
static constexpr uint32_t kNoByteFailMs = 5000;
static constexpr uint32_t kNoSentenceWarnMs = 5000;
static constexpr uint32_t kReportIntervalMs = 5000;

GpsHealthStatus status;
uint32_t lastFixMs = 0;

uint32_t nowMs() {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

int hexValue(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

bool parseChecksum(const char *star, uint8_t *checksum) {
  if (star == nullptr || star[1] == '\0' || star[2] == '\0' || checksum == nullptr) {
    return false;
  }
  const int high = hexValue(star[1]);
  const int low = hexValue(star[2]);
  if (high < 0 || low < 0) {
    return false;
  }
  *checksum = static_cast<uint8_t>((high << 4) | low);
  return true;
}

bool checksumIsValid(const char *sentence) {
  if (sentence == nullptr || sentence[0] != '$') {
    return false;
  }

  const char *star = strchr(sentence, '*');
  uint8_t expected = 0;
  if (!parseChecksum(star, &expected)) {
    return false;
  }

  uint8_t actual = 0;
  for (const char *p = sentence + 1; p < star; ++p) {
    actual ^= static_cast<uint8_t>(*p);
  }
  return actual == expected;
}

bool splitFields(char *payload, char **fields, size_t maxFields, size_t *fieldCount) {
  if (payload == nullptr || fields == nullptr || fieldCount == nullptr || maxFields == 0) {
    return false;
  }

  size_t count = 0;
  fields[count++] = payload;
  for (char *p = payload; *p != '\0' && count < maxFields; ++p) {
    if (*p == ',') {
      *p = '\0';
      fields[count++] = p + 1;
    }
  }
  *fieldCount = count;
  return true;
}

bool sentenceTypeIs(const char *talkerAndType, const char *type) {
  if (talkerAndType == nullptr || type == nullptr || strlen(talkerAndType) < 5) {
    return false;
  }
  return strcmp(talkerAndType + strlen(talkerAndType) - 3, type) == 0;
}

bool parseUnsigned(const char *text, uint32_t *value) {
  if (text == nullptr || *text == '\0' || value == nullptr) {
    return false;
  }

  char *end = nullptr;
  const unsigned long parsed = strtoul(text, &end, 10);
  if (end == text) {
    return false;
  }
  *value = static_cast<uint32_t>(parsed);
  return true;
}

bool parseDouble(const char *text, double *value) {
  if (text == nullptr || *text == '\0' || value == nullptr) {
    return false;
  }

  char *end = nullptr;
  const double parsed = strtod(text, &end);
  if (end == text || !isfinite(parsed)) {
    return false;
  }
  *value = parsed;
  return true;
}

bool parseLatLon(const char *valueText, const char *hemisphere, double *degreesOut) {
  double nmeaDegrees = 0.0;
  if (!parseDouble(valueText, &nmeaDegrees) || hemisphere == nullptr || hemisphere[0] == '\0' || degreesOut == nullptr) {
    return false;
  }

  const int wholeDegrees = static_cast<int>(nmeaDegrees / 100.0);
  const double minutes = nmeaDegrees - static_cast<double>(wholeDegrees * 100);
  double degrees = static_cast<double>(wholeDegrees) + (minutes / 60.0);
  if (hemisphere[0] == 'S' || hemisphere[0] == 'W') {
    degrees = -degrees;
  }
  *degreesOut = degrees;
  return true;
}

void updateFix(const char *latText, const char *latHemisphere, const char *lonText, const char *lonHemisphere) {
  double lat = 0.0;
  double lon = 0.0;
  if (parseLatLon(latText, latHemisphere, &lat) && parseLatLon(lonText, lonHemisphere, &lon)) {
    status.fixSeen = true;
    status.lat = lat;
    status.lon = lon;
    lastFixMs = nowMs();
  }
}

void parseGga(char **fields, size_t fieldCount) {
  if (fieldCount < 10) {
    return;
  }

  uint32_t satellites = 0;
  if (parseUnsigned(fields[7], &satellites)) {
    status.satelliteCount = satellites;
  }

  double hdop = 0.0;
  if (parseDouble(fields[8], &hdop)) {
    status.hdop = hdop;
  }

  uint32_t fixQuality = 0;
  if (parseUnsigned(fields[6], &fixQuality) && fixQuality > 0) {
    updateFix(fields[2], fields[3], fields[4], fields[5]);
  }
}

void parseRmc(char **fields, size_t fieldCount) {
  if (fieldCount < 7 || fields[2][0] != 'A') {
    return;
  }
  updateFix(fields[3], fields[4], fields[5], fields[6]);
}

void parseSentence(const char *sentence) {
  if (!checksumIsValid(sentence)) {
    return;
  }

  status.nmeaSeen = true;
  status.validSentenceCount++;

  char payload[112];
  const char *star = strchr(sentence, '*');
  const size_t payloadLen = static_cast<size_t>(star - (sentence + 1));
  if (payloadLen >= sizeof(payload)) {
    return;
  }
  memcpy(payload, sentence + 1, payloadLen);
  payload[payloadLen] = '\0';

  char *fields[20] = {};
  size_t fieldCount = 0;
  if (!splitFields(payload, fields, 20, &fieldCount) || fieldCount == 0) {
    return;
  }

  if (sentenceTypeIs(fields[0], "GGA")) {
    parseGga(fields, fieldCount);
  } else if (sentenceTypeIs(fields[0], "RMC")) {
    parseRmc(fields, fieldCount);
  }
}

struct NmeaLineParser {
  char line[128] = {};
  size_t length = 0;
  bool collecting = false;

  void feed(char c) {
    if (c == '$') {
      collecting = true;
      length = 0;
      line[length++] = c;
      return;
    }

    if (!collecting) {
      return;
    }

    if (c == '\r') {
      return;
    }

    if (c == '\n') {
      line[length] = '\0';
      parseSentence(line);
      collecting = false;
      length = 0;
      return;
    }

    if (length + 1 < sizeof(line)) {
      line[length++] = c;
    } else {
      collecting = false;
      length = 0;
    }
  }
};

void publishGpsStatus(HealthState state) {
  status.state = state;
  HealthStatus::setGps(status);
}
}  // namespace

extern "C" void GpsHealthTask(void *) {
  Logger::infof("GPS", "UART started at %lu baud RX=%d TX=%d", static_cast<unsigned long>(kGpsBaud), BoardPins::gpsRx, BoardPins::gpsTx);

  uart_config_t uartConfig = {};
  uartConfig.baud_rate = kGpsBaud;
  uartConfig.data_bits = UART_DATA_8_BITS;
  uartConfig.parity = UART_PARITY_DISABLE;
  uartConfig.stop_bits = UART_STOP_BITS_1;
  uartConfig.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  uartConfig.source_clk = UART_SCLK_DEFAULT;

  esp_err_t err = uart_param_config(kGpsUart, &uartConfig);
  if (err == ESP_OK) {
    err = uart_set_pin(kGpsUart, PIN_GPS_TX, PIN_GPS_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  }
  if (err == ESP_OK) {
    err = uart_driver_install(kGpsUart, 2048, 0, 0, nullptr, 0);
  }
  if (err != ESP_OK) {
    status.state = HealthState::Fail;
    HealthStatus::setGps(status);
    Logger::failf("GPS", "UART init failed, esp_err=%d", err);
    vTaskDelete(nullptr);
  }

  NmeaLineParser parser;
  uint32_t lastByteMs = 0;
  uint32_t lastSentenceMs = 0;
  uint32_t lastReportMs = 0;
  uint32_t previousChars = 0;
  uint32_t previousSentences = 0;
  uint32_t previousValidSentenceCount = 0;

  for (;;) {
    uint8_t buffer[128];
    const int bytesRead = uart_read_bytes(kGpsUart, buffer, sizeof(buffer), pdMS_TO_TICKS(20));
    if (bytesRead > 0) {
      for (int i = 0; i < bytesRead; ++i) {
        status.bytesSeen = true;
        status.bytesReceived++;
        lastByteMs = nowMs();
        parser.feed(static_cast<char>(buffer[i]));
      }
    }

    if (status.validSentenceCount != previousValidSentenceCount) {
      previousValidSentenceCount = status.validSentenceCount;
      lastSentenceMs = nowMs();
    }

    const uint32_t currentMs = nowMs();
    if ((currentMs - lastReportMs) >= kReportIntervalMs) {
      lastReportMs = currentMs;

      const uint32_t charCount = status.bytesReceived;
      const uint32_t sentenceCount = status.validSentenceCount;
      const bool newChars = (charCount != previousChars);
      const bool newSentences = (sentenceCount != previousSentences);
      previousChars = charCount;
      previousSentences = sentenceCount;

      if (!status.bytesSeen || (lastByteMs != 0 && (currentMs - lastByteMs) > kNoByteFailMs)) {
        publishGpsStatus(HealthState::Fail);
        Logger::failf("GPS", "No GPS bytes received in the last %lu seconds", static_cast<unsigned long>(kNoByteFailMs / 1000));
      } else if (!status.nmeaSeen || (lastSentenceMs != 0 && (currentMs - lastSentenceMs) > kNoSentenceWarnMs)) {
        publishGpsStatus(HealthState::Warn);
        Logger::warnf("GPS",
                      "GPS bytes are present but valid NMEA sentences are not confirmed yet chars=%lu new_chars=%s",
                      static_cast<unsigned long>(charCount),
                      newChars ? "yes" : "no");
      } else if (!status.fixSeen) {
        publishGpsStatus(HealthState::Warn);
        Logger::okf("GPS", "NMEA data received: sentences=%lu new_sentences=%s", static_cast<unsigned long>(sentenceCount), newSentences ? "yes" : "no");
        Logger::warnf("GPS", "Data received but no location fix yet satellites=%lu hdop=%.2f",
                      static_cast<unsigned long>(status.satelliteCount),
                      status.hdop);
      } else {
        publishGpsStatus(HealthState::Ok);
        Logger::okf("GPS",
                    "Fix: satellites=%lu hdop=%.2f lat=%.6f lon=%.6f age_ms=%lu",
                    static_cast<unsigned long>(status.satelliteCount),
                    status.hdop,
                    status.lat,
                    status.lon,
                    static_cast<unsigned long>(currentMs - lastFixMs));
      }
    }
  }
}
