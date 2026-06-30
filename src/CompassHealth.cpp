#include "BoardPins.h"
#include "HealthStatus.h"
#include "Logger.h"

#include "driver/i2c.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
static constexpr i2c_port_t kI2cPort = I2C_NUM_0;
static constexpr uint32_t kI2cClockHz = 100000;
static constexpr uint8_t kQmc5883Address = 0x0D;
static constexpr uint8_t kRegDataXlsb = 0x00;
static constexpr uint8_t kRegControl1 = 0x09;
static constexpr uint8_t kRegControl2 = 0x0A;
static constexpr uint8_t kRegSetResetPeriod = 0x0B;
static constexpr uint32_t kReportIntervalMs = 2000;

CompassHealthStatus status;

TickType_t i2cTimeout() {
  return pdMS_TO_TICKS(100);
}

bool writeRegister(uint8_t reg, uint8_t value) {
  const uint8_t bytes[2] = {reg, value};
  const esp_err_t err = i2c_master_write_to_device(kI2cPort, kQmc5883Address, bytes, sizeof(bytes), i2cTimeout());
  status.lastError = err;
  return err == ESP_OK;
}

bool readRaw(int16_t *x, int16_t *y, int16_t *z) {
  if (x == nullptr || y == nullptr || z == nullptr) {
    status.lastError = ESP_ERR_INVALID_ARG;
    return false;
  }

  uint8_t raw[6] = {};
  uint8_t reg = kRegDataXlsb;
  const esp_err_t err = i2c_master_write_read_device(kI2cPort, kQmc5883Address, &reg, 1, raw, sizeof(raw), i2cTimeout());
  if (err != ESP_OK) {
    status.lastError = err;
    return false;
  }

  *x = static_cast<int16_t>((static_cast<uint16_t>(raw[1]) << 8) | raw[0]);
  *y = static_cast<int16_t>((static_cast<uint16_t>(raw[3]) << 8) | raw[2]);
  *z = static_cast<int16_t>((static_cast<uint16_t>(raw[5]) << 8) | raw[4]);
  status.lastError = ESP_OK;
  return true;
}

bool deviceResponds() {
  const esp_err_t err = i2c_master_write_to_device(kI2cPort, kQmc5883Address, nullptr, 0, i2cTimeout());
  status.lastError = err;
  return err == ESP_OK;
}

void publishCompassStatus(HealthState state) {
  status.state = state;
  HealthStatus::setCompass(status);
}
}  // namespace

extern "C" void CompassHealthTask(void *) {
  Logger::infof("COMPASS", "I2C started: SDA=%d SCL=%d", BoardPins::i2cSda, BoardPins::i2cScl);

  i2c_config_t config = {};
  config.mode = I2C_MODE_MASTER;
  config.sda_io_num = static_cast<gpio_num_t>(PIN_I2C_SDA);
  config.scl_io_num = static_cast<gpio_num_t>(PIN_I2C_SCL);
  config.sda_pullup_en = GPIO_PULLUP_ENABLE;
  config.scl_pullup_en = GPIO_PULLUP_ENABLE;
  config.master.clk_speed = kI2cClockHz;

  esp_err_t err = i2c_param_config(kI2cPort, &config);
  if (err == ESP_OK) {
    err = i2c_driver_install(kI2cPort, config.mode, 0, 0, 0);
  }
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    status.lastError = err;
    publishCompassStatus(HealthState::Fail);
    Logger::failf("COMPASS", "I2C init failed, esp_err=%d", err);
    vTaskDelete(nullptr);
  }

  bool configured = false;
  int16_t previousX = 0;
  int16_t previousY = 0;
  int16_t previousZ = 0;
  bool havePreviousSample = false;

  for (;;) {
    if (!deviceResponds()) {
      status.detected = false;
      status.writeOk = false;
      status.readOk = false;
      publishCompassStatus(HealthState::Fail);
      Logger::failf("COMPASS", "QMC5883 not found at 0x%02X, i2c_err=%d", kQmc5883Address, status.lastError);
      configured = false;
      vTaskDelay(pdMS_TO_TICKS(kReportIntervalMs));
      continue;
    }

    if (!status.detected) {
      Logger::okf("COMPASS", "QMC5883 detected at 0x%02X", kQmc5883Address);
    }
    status.detected = true;

    if (!configured) {
      const bool resetOk = writeRegister(kRegControl2, 0x80);
      vTaskDelay(pdMS_TO_TICKS(20));
      const bool periodOk = writeRegister(kRegSetResetPeriod, 0x01);
      const bool modeOk = writeRegister(kRegControl1, 0x1D);
      status.writeOk = resetOk && periodOk && modeOk;
      configured = status.writeOk;

      if (!status.writeOk) {
        publishCompassStatus(HealthState::Warn);
        Logger::warnf("COMPASS", "QMC5883 responded but config write failed, i2c_err=%d", status.lastError);
        vTaskDelay(pdMS_TO_TICKS(kReportIntervalMs));
        continue;
      }
      Logger::okf("COMPASS", "QMC5883 config write OK");
    }

    int16_t x = 0;
    int16_t y = 0;
    int16_t z = 0;
    if (!readRaw(&x, &y, &z)) {
      status.readOk = false;
      publishCompassStatus(HealthState::Warn);
      Logger::warnf("COMPASS", "QMC5883 raw data read failed, i2c_err=%d", status.lastError);
      vTaskDelay(pdMS_TO_TICKS(kReportIntervalMs));
      continue;
    }

    status.readOk = true;
    status.rawX = x;
    status.rawY = y;
    status.rawZ = z;

    const bool allZero = (x == 0 && y == 0 && z == 0);
    const bool unchanged = havePreviousSample && x == previousX && y == previousY && z == previousZ;
    havePreviousSample = true;
    previousX = x;
    previousY = y;
    previousZ = z;

    if (allZero || unchanged) {
      publishCompassStatus(HealthState::Warn);
      Logger::warnf("COMPASS", "QMC5883 read/write OK but raw data looks suspicious: X=%d Y=%d Z=%d", x, y, z);
    } else {
      publishCompassStatus(HealthState::Ok);
      Logger::infof("COMPASS", "Raw magnetic field: X=%d Y=%d Z=%d", x, y, z);
    }

    vTaskDelay(pdMS_TO_TICKS(kReportIntervalMs));
  }
}
