#include "BoardPins.h"
#include "ControlChannel.h"
#include "HealthStatus.h"
#include "Logger.h"
#include "NodeConfig.h"

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifndef NAV_ENABLE_GNSS
#define NAV_ENABLE_GNSS 1
#endif

extern "C" void RadioHealthTask(void *param);
extern "C" void GpsHealthTask(void *param);

namespace {
const char *pinLabel(int pin, char *buffer, size_t bufferSize) {
  if (pin == BoardPins::notConnected) {
    return "NC";
  }
  snprintf(buffer, bufferSize, "%d", pin);
  return buffer;
}

void printPinMap() {
  char dio2Label[8];
  char dio3Label[8];

  Logger::infof("SYSTEM", "E28/SX128x pin map:");
  Logger::infof("SYSTEM",
                "  SCK=%d MISO=%d MOSI=%d CS=%d",
                BoardPins::loraSck,
                BoardPins::loraMiso,
                BoardPins::loraMosi,
                BoardPins::loraCs);
  Logger::infof("SYSTEM",
                "  RST=%d BUSY=%d DIO1=%d DIO2=%s DIO3=%s",
                BoardPins::loraRst,
                BoardPins::loraBusy,
                BoardPins::loraDio1,
                pinLabel(BoardPins::loraDio2, dio2Label, sizeof(dio2Label)),
                pinLabel(BoardPins::loraDio3, dio3Label, sizeof(dio3Label)));
  Logger::infof("SYSTEM", "  GPS RX=%d TX=%d", BoardPins::gpsRx, BoardPins::gpsTx);
}

#if !NAV_ENABLE_GNSS
void markGpsDisabled() {
  GpsHealthStatus status = {};
  status.state = HealthState::Disabled;
  HealthStatus::setGps(status);
  Logger::infof("GPS", "disabled in this firmware");
}
#endif

void HealthReporterTask(void *) {
  for (;;) {
    SystemHealth health = HealthStatus::snapshot();
    Logger::infof("SYSTEM",
                  "Health summary: RADIO=%s GPS=%s | radio tx=%lu rx=%lu gps_bytes=%lu gps_sentences=%lu gps_fix=%d",
                  HealthStatus::toString(health.radio.state),
                  HealthStatus::toString(health.gps.state),
                  static_cast<unsigned long>(health.radio.txOkCount),
                  static_cast<unsigned long>(health.radio.rxOkCount),
                  static_cast<unsigned long>(health.gps.bytesReceived),
                  static_cast<unsigned long>(health.gps.validSentenceCount),
                  static_cast<int>(health.gps.fixType));
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}
}  // namespace

extern "C" void app_main(void) {
  vTaskDelay(pdMS_TO_TICKS(1500));

  Logger::begin();
  HealthStatus::begin();

  Logger::infof("SYSTEM", "Booting firmware");
  Logger::infof("SYSTEM", "Board: %s", BoardPins::boardName);
  Logger::infof("SYSTEM", "Build: %s %s", __DATE__, __TIME__);
  printPinMap();

  // Node application: persisted config + navigation core + control channel.
  NodeConfig config = NodeConfigStore::load(/*defaultNodeId=*/0u);
  ControlChannel::begin(config);

  // Boot self-test and distance-only ranging bring-up.
  xTaskCreate(RadioHealthTask, "RadioHealthTask", 8192, nullptr, 2, nullptr);

#if NAV_ENABLE_GNSS
  xTaskCreate(GpsHealthTask, "GpsHealthTask", 6144, nullptr, 1, nullptr);
#else
  markGpsDisabled();
#endif

  xTaskCreate(HealthReporterTask, "HealthReporterTask", 4096, nullptr, 1, nullptr);

  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
