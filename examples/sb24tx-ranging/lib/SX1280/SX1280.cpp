#include "SX1280.h"

#include <SPI.h>

static constexpr double FREQ_STEP = 52000000.0 / 262144.0;

static constexpr uint16_t REG_LORA_SF_CONFIG = 0x0925;
static constexpr uint16_t REG_FREQ_ERROR_COMP = 0x093C;
static constexpr uint16_t REG_RANGING_REQUEST_ADDR = 0x0912;
static constexpr uint16_t REG_RANGING_DEVICE_ADDR = 0x0916;
static constexpr uint16_t REG_RANGING_FILTER_WINDOW = 0x091E;
static constexpr uint16_t REG_RANGING_RESET_FILTER = 0x0923;
static constexpr uint16_t REG_RANGING_RESULT_MUX = 0x0924;
static constexpr uint16_t REG_RANGING_CALIB_MSB = 0x092C;
static constexpr uint16_t REG_RANGING_CALIB_LSB = 0x092D;
static constexpr uint16_t REG_RANGING_ID_CHECK = 0x0931;
static constexpr uint16_t REG_RANGING_RESULT_2 = 0x0961;
static constexpr uint16_t REG_LORA_MEMORY_CTRL = 0x097F;

static constexpr uint8_t LORA_SF_7 = 0x70;
static constexpr uint8_t LORA_BW_1600 = 0x0A;  // 1625 kHz
static constexpr uint8_t LORA_CR_4_5 = 0x01;
static constexpr uint8_t LORA_EXPLICIT_HEADER = 0x00;
static constexpr uint8_t LORA_CRC_ENABLE = 0x20;
static constexpr uint8_t LORA_IQ_STD = 0x40;
static constexpr uint8_t LORA_PREAMBLE_12_SYMBOLS = 0x0C;
static constexpr uint8_t LORA_PAYLOAD_LENGTH_8 = 0x08;
static constexpr uint8_t RADIO_RAMP_20_US = 0xE0;
static constexpr uint8_t RADIO_TX_POWER_10_DBM = 28;  // Pout = -18 + power
static constexpr float RANGING_BW_MHZ = 1.625f;

static SPISettings kSpi(8000000, MSBFIRST, SPI_MODE0);

SX1280::SX1280(int8_t nss, int8_t rst, int8_t busy)
    : _nss(nss), _rst(rst), _busy(busy) {}

bool SX1280::waitBusy(uint32_t timeout_us) {
  uint32_t start = micros();
  while (digitalRead(_busy)) {
    if ((uint32_t)(micros() - start) > timeout_us) {
      return false;
    }
    yield();
  }
  return true;
}

void SX1280::reset() {
  pinMode(_rst, OUTPUT);
  digitalWrite(_rst, LOW);
  delayMicroseconds(100);
  digitalWrite(_rst, HIGH);
  delay(5);
  waitBusy();
}

void SX1280::writeCommand(uint8_t opcode, const uint8_t *params, uint8_t n) {
  waitBusy();
  SPI.beginTransaction(kSpi);
  digitalWrite(_nss, LOW);
  SPI.transfer(opcode);
  for (uint8_t i = 0; i < n; i++) {
    SPI.transfer(params[i]);
  }
  digitalWrite(_nss, HIGH);
  SPI.endTransaction();
}

void SX1280::readCommand(uint8_t opcode, uint8_t *result, uint8_t n) {
  waitBusy();
  SPI.beginTransaction(kSpi);
  digitalWrite(_nss, LOW);
  SPI.transfer(opcode);
  SPI.transfer(0x00);
  for (uint8_t i = 0; i < n; i++) {
    result[i] = SPI.transfer(0x00);
  }
  digitalWrite(_nss, HIGH);
  SPI.endTransaction();
}

void SX1280::writeRegister(uint16_t addr, uint8_t value) {
  uint8_t p[3] = {
      (uint8_t)(addr >> 8),
      (uint8_t)(addr & 0xFF),
      value,
  };
  writeCommand(OP_WRITE_REGISTER, p, 3);
}

uint8_t SX1280::readRegister(uint16_t addr) {
  waitBusy();
  SPI.beginTransaction(kSpi);
  digitalWrite(_nss, LOW);
  SPI.transfer(OP_READ_REGISTER);
  SPI.transfer((uint8_t)(addr >> 8));
  SPI.transfer((uint8_t)(addr & 0xFF));
  SPI.transfer(0x00);
  uint8_t value = SPI.transfer(0x00);
  digitalWrite(_nss, HIGH);
  SPI.endTransaction();
  return value;
}

uint8_t SX1280::getStatus() {
  uint8_t status = 0;
  waitBusy();
  SPI.beginTransaction(kSpi);
  digitalWrite(_nss, LOW);
  SPI.transfer(OP_GET_STATUS);
  status = SPI.transfer(0x00);
  digitalWrite(_nss, HIGH);
  SPI.endTransaction();
  return status;
}

bool SX1280::beginRanging(RangingRole role, uint32_t frequency_hz,
                          uint32_t ranging_address, uint16_t calibration) {
  pinMode(_nss, OUTPUT);
  digitalWrite(_nss, HIGH);
  pinMode(_busy, INPUT);

  SPI.begin();
  reset();
  setStandby(STDBY_RC);

  uint8_t regulator = 0x00;  // LDO mode, conservative for bring-up.
  writeCommand(OP_SET_REGULATOR_MODE, &regulator, 1);

  uint8_t packet_type = PACKET_TYPE_RANGING;
  writeCommand(OP_SET_PACKET_TYPE, &packet_type, 1);
  setFrequencyHz(frequency_hz);

  uint8_t buffer_base[2] = {0x00, 0x80};
  writeCommand(OP_SET_BUFFER_BASE_ADDRESS, buffer_base, 2);

  uint8_t modulation[3] = {LORA_SF_7, LORA_BW_1600, LORA_CR_4_5};
  writeCommand(OP_SET_MODULATION_PARAMS, modulation, 3);
  writeRegister(REG_LORA_SF_CONFIG, 0x37);
  writeRegister(REG_FREQ_ERROR_COMP, 0x01);

  uint8_t packet[7] = {
      LORA_PREAMBLE_12_SYMBOLS,
      LORA_EXPLICIT_HEADER,
      LORA_PAYLOAD_LENGTH_8,
      LORA_CRC_ENABLE,
      LORA_IQ_STD,
      0x00,
      0x00,
  };
  writeCommand(OP_SET_PACKET_PARAMS, packet, 7);

  uint8_t tx_params[2] = {RADIO_TX_POWER_10_DBM, RADIO_RAMP_20_US};
  writeCommand(OP_SET_TX_PARAMS, tx_params, 2);

  setRangingAddress(role, ranging_address);
  setRangingCalibration(calibration);

  uint8_t filter_window = 8;
  writeRegister(REG_RANGING_FILTER_WINDOW, filter_window);
  writeRegister(REG_RANGING_RESET_FILTER,
                readRegister(REG_RANGING_RESET_FILTER) | (1u << 6));

  uint16_t irq_mask = IRQ_RX_TX_TIMEOUT;
  if (role == RANGING_ROLE_MASTER) {
    irq_mask |= IRQ_RANGING_MASTER_RESULT_VALID | IRQ_RANGING_MASTER_TIMEOUT;
  } else {
    irq_mask |= IRQ_RANGING_SLAVE_RESPONSE_DONE |
                IRQ_RANGING_SLAVE_REQUEST_DISCARD |
                IRQ_RANGING_SLAVE_REQUEST_VALID;
  }
  setDioIrqParams(irq_mask, irq_mask, 0, 0);

  uint8_t role_param = (uint8_t)role;
  writeCommand(OP_SET_RANGING_ROLE, &role_param, 1);
  clearIrqStatus();

  uint8_t status = getStatus();
  return status != 0x00 && status != 0xFF;
}

void SX1280::setStandby(Standby mode) {
  uint8_t param = (uint8_t)mode;
  writeCommand(OP_SET_STANDBY, &param, 1);
}

void SX1280::setFrequencyHz(uint32_t freq_hz) {
  uint32_t frf = (uint32_t)((double)freq_hz / FREQ_STEP);
  uint8_t p[3] = {
      (uint8_t)(frf >> 16),
      (uint8_t)(frf >> 8),
      (uint8_t)(frf),
  };
  writeCommand(OP_SET_RF_FREQUENCY, p, 3);
}

void SX1280::setDioIrqParams(uint16_t irq_mask, uint16_t dio1_mask,
                             uint16_t dio2_mask, uint16_t dio3_mask) {
  uint8_t p[8] = {
      (uint8_t)(irq_mask >> 8),
      (uint8_t)(irq_mask),
      (uint8_t)(dio1_mask >> 8),
      (uint8_t)(dio1_mask),
      (uint8_t)(dio2_mask >> 8),
      (uint8_t)(dio2_mask),
      (uint8_t)(dio3_mask >> 8),
      (uint8_t)(dio3_mask),
  };
  writeCommand(OP_SET_DIO_IRQ_PARAMS, p, 8);
}

void SX1280::setRangingAddress(RangingRole role, uint32_t address) {
  uint16_t base = (role == RANGING_ROLE_MASTER) ? REG_RANGING_REQUEST_ADDR
                                                : REG_RANGING_DEVICE_ADDR;
  writeRegister(base + 0, (uint8_t)(address >> 24));
  writeRegister(base + 1, (uint8_t)(address >> 16));
  writeRegister(base + 2, (uint8_t)(address >> 8));
  writeRegister(base + 3, (uint8_t)(address));

  if (role == RANGING_ROLE_SLAVE) {
    uint8_t value = readRegister(REG_RANGING_ID_CHECK);
    writeRegister(REG_RANGING_ID_CHECK, (value & 0x3F) | 0xC0);
  }
}

void SX1280::setRangingCalibration(uint16_t calibration) {
  writeRegister(REG_RANGING_CALIB_MSB, (uint8_t)(calibration >> 8));
  writeRegister(REG_RANGING_CALIB_LSB, (uint8_t)(calibration));
}

void SX1280::startMasterExchange(uint16_t timeout_ms) {
  clearIrqStatus();
  setStandby(STDBY_RC);
  uint16_t ticks = timeout_ms;
  uint8_t p[3] = {0x02, (uint8_t)(ticks >> 8), (uint8_t)(ticks)};
  writeCommand(OP_SET_TX, p, 3);
}

void SX1280::startSlaveListen() {
  clearIrqStatus();
  setStandby(STDBY_RC);
  uint8_t p[3] = {0x00, 0xFF, 0xFF};
  writeCommand(OP_SET_RX, p, 3);
}

uint16_t SX1280::getIrqStatus() {
  uint8_t irq[2] = {0, 0};
  readCommand(OP_GET_IRQ_STATUS, irq, 2);
  return ((uint16_t)irq[0] << 8) | irq[1];
}

void SX1280::clearIrqStatus(uint16_t irq_mask) {
  uint8_t p[2] = {
      (uint8_t)(irq_mask >> 8),
      (uint8_t)(irq_mask),
  };
  writeCommand(OP_CLEAR_IRQ_STATUS, p, 2);
}

int32_t SX1280::readRangingResultRaw() {
  setStandby(STDBY_XOSC);
  writeRegister(REG_LORA_MEMORY_CTRL, readRegister(REG_LORA_MEMORY_CTRL) | 0x02);
  writeRegister(REG_RANGING_RESULT_MUX,
                readRegister(REG_RANGING_RESULT_MUX) & 0xCF);

  uint32_t raw = ((uint32_t)readRegister(REG_RANGING_RESULT_2) << 16) |
                 ((uint32_t)readRegister(REG_RANGING_RESULT_2 + 1) << 8) |
                 (uint32_t)readRegister(REG_RANGING_RESULT_2 + 2);
  setStandby(STDBY_RC);

  if (raw & 0x00800000UL) {
    raw |= 0xFF000000UL;
  }
  return (int32_t)raw;
}

SX1280::PacketStatus SX1280::readPacketStatus() {
  uint8_t status[2] = {0, 0};
  readCommand(OP_GET_PACKET_STATUS, status, 2);

  int8_t snr_raw = (int8_t)status[1];
  PacketStatus packet_status = {
      (int16_t)(-((int16_t)status[0]) / 2),
      ((float)snr_raw) / 4.0f,
  };
  return packet_status;
}

float SX1280::rawRangingMeters(int32_t raw_result) const {
  return ((float)raw_result * 150.0f) / (4096.0f * RANGING_BW_MHZ);
}
