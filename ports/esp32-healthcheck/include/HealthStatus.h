#pragma once

#include <Arduino.h>

enum class HealthState : uint8_t {
  Unknown,
  Ok,
  Warn,
  Fail,
};

struct RadioHealthStatus {
  HealthState state = HealthState::Unknown;
  bool initialized = false;
  bool txOk = false;
  bool rxOk = false;
  uint32_t txOkCount = 0;
  uint32_t rxOkCount = 0;
  int lastError = 0;
  float lastRssiDbm = 0.0f;
  float lastSnrDb = 0.0f;
};

struct GpsHealthStatus {
  HealthState state = HealthState::Unknown;
  bool bytesSeen = false;
  bool nmeaSeen = false;
  bool fixSeen = false;
  uint32_t bytesReceived = 0;
  uint32_t validSentenceCount = 0;
  uint32_t satelliteCount = 0;
  double hdop = 0.0;
  double lat = 0.0;
  double lon = 0.0;
};

struct CompassHealthStatus {
  HealthState state = HealthState::Unknown;
  bool detected = false;
  bool writeOk = false;
  bool readOk = false;
  int16_t rawX = 0;
  int16_t rawY = 0;
  int16_t rawZ = 0;
  int lastError = 0;
};

struct SystemHealth {
  RadioHealthStatus radio;
  GpsHealthStatus gps;
  CompassHealthStatus compass;
};

namespace HealthStatus {
void begin();
void setRadio(const RadioHealthStatus &status);
void setGps(const GpsHealthStatus &status);
void setCompass(const CompassHealthStatus &status);
SystemHealth snapshot();
const char *toString(HealthState state);
}  // namespace HealthStatus
