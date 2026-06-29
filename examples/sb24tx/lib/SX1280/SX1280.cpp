#include "SX1280.h"
#include <SPI.h>

// PLL frequency step = F_XTAL / 2^18, with F_XTAL = 52 MHz.
//   freq_reg = freq_Hz / FREQ_STEP
static constexpr double FREQ_STEP = 52000000.0 / 262144.0;  // ~198.3643 Hz

// High-sensitivity LNA register (datasheet 4.2 / errata).
static constexpr uint16_t REG_LNA_REGIME = 0x0891;

// SPI: SX1280 supports up to 18 MHz, mode 0, MSB first.
static SPISettings kSpi(8000000, MSBFIRST, SPI_MODE0);

SX1280::SX1280(int8_t nss, int8_t rst, int8_t busy)
    : _nss(nss), _rst(rst), _busy(busy) {}

// ---------------------------------------------------------------------------
// Low-level helpers
// ---------------------------------------------------------------------------
void SX1280::waitBusy(uint32_t timeoutUs) {
  uint32_t start = micros();
  while (digitalRead(_busy)) {
    if ((uint32_t)(micros() - start) > timeoutUs) return;  // bail rather than hang
    yield();
  }
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
  for (uint8_t i = 0; i < n; i++) SPI.transfer(params[i]);
  digitalWrite(_nss, HIGH);
  SPI.endTransaction();
}

void SX1280::readCommand(uint8_t opcode, uint8_t *result, uint8_t n) {
  waitBusy();
  SPI.beginTransaction(kSpi);
  digitalWrite(_nss, LOW);
  SPI.transfer(opcode);
  SPI.transfer(0x00);                       // status byte (discarded)
  for (uint8_t i = 0; i < n; i++) result[i] = SPI.transfer(0x00);
  digitalWrite(_nss, HIGH);
  SPI.endTransaction();
}

void SX1280::writeRegister(uint16_t addr, uint8_t value) {
  uint8_t p[3] = {(uint8_t)(addr >> 8), (uint8_t)(addr & 0xFF), value};
  writeCommand(OP_WRITE_REGISTER, p, 3);
}

uint8_t SX1280::readRegister(uint16_t addr) {
  waitBusy();
  SPI.beginTransaction(kSpi);
  digitalWrite(_nss, LOW);
  SPI.transfer(OP_READ_REGISTER);
  SPI.transfer((uint8_t)(addr >> 8));
  SPI.transfer((uint8_t)(addr & 0xFF));
  SPI.transfer(0x00);                       // status byte (discarded)
  uint8_t v = SPI.transfer(0x00);
  digitalWrite(_nss, HIGH);
  SPI.endTransaction();
  return v;
}

uint8_t SX1280::getStatus() {
  uint8_t s = 0;
  waitBusy();
  SPI.beginTransaction(kSpi);
  digitalWrite(_nss, LOW);
  SPI.transfer(OP_GET_STATUS);
  s = SPI.transfer(0x00);
  digitalWrite(_nss, HIGH);
  SPI.endTransaction();
  return s;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
bool SX1280::begin(GfskBrBw rxBandwidth) {
  pinMode(_nss, OUTPUT);
  digitalWrite(_nss, HIGH);
  pinMode(_busy, INPUT);

  SPI.begin();  // ESP8266/8285 HSPI: SCK=14, MISO=12, MOSI=13

  reset();

  setStandby(STDBY_RC);

  uint8_t reg = 0x00;                                 // 0x00 = LDO, 0x01 = DC-DC
  writeCommand(OP_SET_REGULATOR_MODE, &reg, 1);

  uint8_t pkt = PACKET_TYPE_GFSK;
  writeCommand(OP_SET_PACKET_TYPE, &pkt, 1);

  // SetModulationParams(GFSK): [BR/BW], [mod index], [shaping].
  // Only the bandwidth matters for an RSSI scan; index/shaping are nominal.
  uint8_t mod[3] = {(uint8_t)rxBandwidth, 0x09 /*MOD_IND_0_50*/, 0x00 /*BT_OFF*/};
  writeCommand(OP_SET_MODULATION, mod, 3);

  // Sanity check: status byte should be readable and non-0xFF (bus alive).
  uint8_t s = getStatus();
  return s != 0x00 && s != 0xFF;
}

void SX1280::setStandby(Standby mode) {
  uint8_t m = (uint8_t)mode;
  writeCommand(OP_SET_STANDBY, &m, 1);
}

void SX1280::setFrequencyHz(uint32_t freqHz) {
  uint32_t frf = (uint32_t)((double)freqHz / FREQ_STEP);
  uint8_t p[3] = {(uint8_t)(frf >> 16), (uint8_t)(frf >> 8), (uint8_t)(frf)};
  writeCommand(OP_SET_RF_FREQUENCY, p, 3);
}

void SX1280::startRx() {
  // SetRx(periodBase=0x00, periodBaseCount=0xFFFF) => continuous receive.
  uint8_t p[3] = {0x00, 0xFF, 0xFF};
  writeCommand(OP_SET_RX, p, 3);
}

float SX1280::readRssiDbm() {
  uint8_t raw = 0;
  readCommand(OP_GET_RSSI_INST, &raw, 1);
  return -((float)raw) / 2.0f;              // datasheet: RSSI[dBm] = -rssiInst/2
}

void SX1280::enableHighSensitivity() {
  uint8_t v = readRegister(REG_LNA_REGIME);
  writeRegister(REG_LNA_REGIME, v | 0xC0);  // set bits [7:6] = high-gain LNA
}
