#include "BoardPins.h"
#include "HealthStatus.h"
#include "Logger.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" void RadioHealthTask(void *param);
extern "C" void GpsHealthTask(void *param);
extern "C" void CompassHealthTask(void *param);

namespace {
void printPinMap() {
  Logger::infof("SYSTEM", "Pin map:");
  Logger::infof("SYSTEM",
                "  LORA SCK=%d MISO=%d MOSI=%d CS=%d RST=%d BUSY=%d DIO1=%d DIO2=%d DIO3=%d",
                BoardPins::loraSck,
                BoardPins::loraMiso,
                BoardPins::loraMosi,
                BoardPins::loraCs,
                BoardPins::loraRst,
                BoardPins::loraBusy,
                BoardPins::loraDio1,
                BoardPins::loraDio2,
                BoardPins::loraDio3);
  Logger::infof("SYSTEM", "  GPS RX=%d TX=%d", BoardPins::gpsRx, BoardPins::gpsTx);
  Logger::infof("SYSTEM", "  I2C SDA=%d SCL=%d", BoardPins::i2cSda, BoardPins::i2cScl);
}

void HealthReporterTask(void *) {
  for (;;) {
    SystemHealth health = HealthStatus::snapshot();
    Logger::infof("SYSTEM",
                  "Health summary: RADIO=%s GPS=%s COMPASS=%s | radio tx=%lu rx=%lu gps_bytes=%lu gps_sentences=%lu compass_xyz=%d,%d,%d",
                  HealthStatus::toString(health.radio.state),
                  HealthStatus::toString(health.gps.state),
                  HealthStatus::toString(health.compass.state),
                  static_cast<unsigned long>(health.radio.txOkCount),
                  static_cast<unsigned long>(health.radio.rxOkCount),
                  static_cast<unsigned long>(health.gps.bytesReceived),
                  static_cast<unsigned long>(health.gps.validSentenceCount),
                  health.compass.rawX,
                  health.compass.rawY,
                  health.compass.rawZ);
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

  xTaskCreate(RadioHealthTask, "RadioHealthTask", 8192, nullptr, 2, nullptr);
  xTaskCreate(GpsHealthTask, "GpsHealthTask", 6144, nullptr, 1, nullptr);
  xTaskCreate(CompassHealthTask, "CompassHealthTask", 4096, nullptr, 1, nullptr);
  xTaskCreate(HealthReporterTask, "HealthReporterTask", 4096, nullptr, 1, nullptr);

  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
