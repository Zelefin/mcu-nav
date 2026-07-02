#include "HealthStatus.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace {
SemaphoreHandle_t gHealthMutex = nullptr;
SystemHealth gHealth;
}  // namespace

namespace HealthStatus {
void begin() {
  if (gHealthMutex == nullptr) {
    gHealthMutex = xSemaphoreCreateMutex();
  }
}

void setRadio(const RadioHealthStatus &status) {
  if (gHealthMutex != nullptr) {
    xSemaphoreTake(gHealthMutex, portMAX_DELAY);
  }
  gHealth.radio = status;
  if (gHealthMutex != nullptr) {
    xSemaphoreGive(gHealthMutex);
  }
}

void setGps(const GpsHealthStatus &status) {
  if (gHealthMutex != nullptr) {
    xSemaphoreTake(gHealthMutex, portMAX_DELAY);
  }
  gHealth.gps = status;
  if (gHealthMutex != nullptr) {
    xSemaphoreGive(gHealthMutex);
  }
}

void setCompass(const CompassHealthStatus &status) {
  if (gHealthMutex != nullptr) {
    xSemaphoreTake(gHealthMutex, portMAX_DELAY);
  }
  gHealth.compass = status;
  if (gHealthMutex != nullptr) {
    xSemaphoreGive(gHealthMutex);
  }
}

SystemHealth snapshot() {
  if (gHealthMutex != nullptr) {
    xSemaphoreTake(gHealthMutex, portMAX_DELAY);
  }
  SystemHealth copy = gHealth;
  if (gHealthMutex != nullptr) {
    xSemaphoreGive(gHealthMutex);
  }
  return copy;
}

const char *toString(HealthState state) {
  switch (state) {
    case HealthState::Unknown:
      return "UNKNOWN";
    case HealthState::Disabled:
      return "DISABLED";
    case HealthState::Ok:
      return "OK";
    case HealthState::Warn:
      return "WARN";
    case HealthState::Fail:
      return "FAIL";
  }
  return "UNKNOWN";
}
}  // namespace HealthStatus
