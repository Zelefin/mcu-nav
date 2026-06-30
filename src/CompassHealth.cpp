#include <Arduino.h>
#include <Wire.h>

#include "BoardPins.h"
#include "HealthStatus.h"
#include "Logger.h"

namespace {
static constexpr uint8_t kQmc5883Address = 0x0D;
static constexpr uint8_t kRegDataXlsb = 0x00;
static constexpr uint8_t kRegControl1 = 0x09;
static constexpr uint8_t kRegControl2 = 0x0A;
static constexpr uint8_t kRegSetResetPeriod = 0x0B;
static constexpr uint32_t kReportIntervalMs = 2000;

CompassHealthStatus status;

bool writeRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(kQmc5883Address);
  Wire.write(reg);
  Wire.write(value);
  const uint8_t err = Wire.endTransmission();
  status.lastError = err;
  return err == 0;
}

bool readRaw(int16_t *x, int16_t *y, int16_t *z) {
  Wire.beginTransmission(kQmc5883Address);
  Wire.write(kRegDataXlsb);
  uint8_t err = Wire.endTransmission(false);
  if (err != 0) {
    status.lastError = err;
    return false;
  }

  const uint8_t received = Wire.requestFrom(kQmc5883Address, static_cast<uint8_t>(6));
  if (received != 6) {
    status.lastError = received;
    return false;
  }

  const uint8_t xLsb = Wire.read();
  const uint8_t xMsb = Wire.read();
  const uint8_t yLsb = Wire.read();
  const uint8_t yMsb = Wire.read();
  const uint8_t zLsb = Wire.read();
  const uint8_t zMsb = Wire.read();

  *x = static_cast<int16_t>((static_cast<uint16_t>(xMsb) << 8) | xLsb);
  *y = static_cast<int16_t>((static_cast<uint16_t>(yMsb) << 8) | yLsb);
  *z = static_cast<int16_t>((static_cast<uint16_t>(zMsb) << 8) | zLsb);
  status.lastError = 0;
  return true;
}

bool deviceResponds() {
  Wire.beginTransmission(kQmc5883Address);
  const uint8_t err = Wire.endTransmission();
  status.lastError = err;
  return err == 0;
}

void publishCompassStatus(HealthState state) {
  status.state = state;
  HealthStatus::setCompass(status);
}
}  // namespace

extern "C" void CompassHealthTask(void *) {
  Logger::infof("COMPASS", "I2C started: SDA=%d SCL=%d", BoardPins::i2cSda, BoardPins::i2cScl);
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(100000);

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
