#include <Arduino.h>
#include <TinyGPSPlus.h>

#include "BoardPins.h"
#include "HealthStatus.h"
#include "Logger.h"

namespace {
static constexpr uint32_t kGpsBaud = 115200;
static constexpr uint32_t kNoByteFailMs = 5000;
static constexpr uint32_t kNoSentenceWarnMs = 5000;
static constexpr uint32_t kReportIntervalMs = 5000;

HardwareSerial gpsSerial(2);
TinyGPSPlus gps;
GpsHealthStatus status;

void publishGpsStatus(HealthState state) {
  status.state = state;
  status.satelliteCount = gps.satellites.isValid() ? gps.satellites.value() : 0;
  status.hdop = gps.hdop.isValid() ? gps.hdop.hdop() : 0.0;
  status.fixSeen = gps.location.isValid();
  if (gps.location.isValid()) {
    status.lat = gps.location.lat();
    status.lon = gps.location.lng();
  }
  HealthStatus::setGps(status);
}
}  // namespace

extern "C" void GpsHealthTask(void *) {
  Logger::infof("GPS", "UART started at %lu baud RX=%d TX=%d", static_cast<unsigned long>(kGpsBaud), BoardPins::gpsRx, BoardPins::gpsTx);
  gpsSerial.begin(kGpsBaud, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);

  uint32_t lastByteMs = 0;
  uint32_t lastSentenceMs = 0;
  uint32_t lastReportMs = 0;
  uint32_t previousChars = 0;
  uint32_t previousSentences = 0;

  for (;;) {
    while (gpsSerial.available() > 0) {
      const char c = static_cast<char>(gpsSerial.read());
      status.bytesSeen = true;
      status.bytesReceived++;
      lastByteMs = millis();
      if (gps.encode(c)) {
        status.nmeaSeen = true;
        status.validSentenceCount++;
        lastSentenceMs = millis();
      }
    }

    const uint32_t nowMs = millis();
    if ((nowMs - lastReportMs) >= kReportIntervalMs) {
      lastReportMs = nowMs;

      const uint32_t charCount = gps.charsProcessed();
      const uint32_t sentenceCount = gps.passedChecksum();
      const bool newChars = (charCount != previousChars);
      const bool newSentences = (sentenceCount != previousSentences);
      previousChars = charCount;
      previousSentences = sentenceCount;

      if (!status.bytesSeen || (lastByteMs != 0 && (nowMs - lastByteMs) > kNoByteFailMs)) {
        publishGpsStatus(HealthState::Fail);
        Logger::failf("GPS", "No GPS bytes received in the last %lu seconds", static_cast<unsigned long>(kNoByteFailMs / 1000));
      } else if (!status.nmeaSeen || (lastSentenceMs != 0 && (nowMs - lastSentenceMs) > kNoSentenceWarnMs)) {
        publishGpsStatus(HealthState::Warn);
        Logger::warnf("GPS",
                      "GPS bytes are present but valid NMEA sentences are not confirmed yet chars=%lu new_chars=%s",
                      static_cast<unsigned long>(charCount),
                      newChars ? "yes" : "no");
      } else if (!gps.location.isValid()) {
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
                    static_cast<unsigned long>(gps.location.age()));
      }
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
