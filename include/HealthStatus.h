#pragma once

#include <stdint.h>

#include "nav/nav_types.h"

enum class HealthState : uint8_t {
  Unknown,
  Disabled,
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
  nav_gnss_fix_type_t fixType = NAV_GNSS_FIX_NONE;
  uint32_t bytesReceived = 0;
  uint32_t validSentenceCount = 0;
  uint32_t satelliteCount = 0;
  uint16_t hdopCenti = 0;
  int32_t latE7 = 0;
  int32_t lonE7 = 0;
  int32_t altMm = 0;
};

struct SystemHealth {
  RadioHealthStatus radio;
  GpsHealthStatus gps;
};

namespace HealthStatus {
void begin();
void setRadio(const RadioHealthStatus &status);
void setGps(const GpsHealthStatus &status);
SystemHealth snapshot();
const char *toString(HealthState state);
}  // namespace HealthStatus
