// ---------------------------------------------------------------------------
// Minimal SX1280 driver - just enough to scan the 2.4 GHz band by reading the
// instantaneous RSSI of the receiver front-end. No packet handling.
//
// Datasheet: Semtech SX1280/SX1281 (DS_SX1280-1_V3.2)
// ---------------------------------------------------------------------------
#pragma once

#include <Arduino.h>

class SX1280 {
public:
  // SPI command opcodes (subset)
  enum Opcode : uint8_t {
    OP_GET_STATUS         = 0xC0,
    OP_WRITE_REGISTER     = 0x18,
    OP_READ_REGISTER      = 0x19,
    OP_SET_STANDBY        = 0x80,
    OP_SET_RX             = 0x82,
    OP_SET_FS             = 0xC1,
    OP_SET_PACKET_TYPE    = 0x8A,
    OP_SET_RF_FREQUENCY   = 0x86,
    OP_SET_MODULATION     = 0x8B,
    OP_SET_REGULATOR_MODE = 0x96,
    OP_GET_RSSI_INST      = 0x1F,
  };

  // Packet types
  enum PacketType : uint8_t {
    PACKET_TYPE_GFSK = 0x00,
    PACKET_TYPE_LORA = 0x01,
  };

  // Standby clock source
  enum Standby : uint8_t {
    STDBY_RC   = 0x00,
    STDBY_XOSC = 0x01,
  };

  // GFSK bitrate/bandwidth combos (1st SetModulationParams arg).
  // The bandwidth (the "BW_x_y" part, in MHz double-sideband) is what sets the
  // resolution of an RSSI scan; the bitrate is irrelevant when only reading RSSI.
  enum GfskBrBw : uint8_t {
    GFSK_BR_2_000_BW_2_4 = 0x04,  // 2.4 MHz
    GFSK_BR_1_600_BW_2_4 = 0x28,  // 2.4 MHz
    GFSK_BR_1_000_BW_2_4 = 0x4C,  // 2.4 MHz
    GFSK_BR_1_000_BW_1_2 = 0x45,  // 1.2 MHz
    GFSK_BR_0_800_BW_1_2 = 0x69,  // 1.2 MHz
    GFSK_BR_0_500_BW_0_6 = 0x86,  // 0.6 MHz
    GFSK_BR_0_250_BW_0_3 = 0xC7,  // 0.3 MHz
  };

  SX1280(int8_t nss, int8_t rst, int8_t busy);

  // Bring the radio up in RX-continuous GFSK mode ready for RSSI sampling.
  // rxBandwidth selects the receiver bandwidth == scan bin width.
  bool begin(GfskBrBw rxBandwidth = GFSK_BR_1_000_BW_1_2);

  // Retune. Frequency in Hz (2.4 GHz band: 2_400_000_000 .. 2_500_000_000).
  void setFrequencyHz(uint32_t freqHz);

  // Enter continuous-receive so the front-end is actively measuring.
  void startRx();
  void setStandby(Standby mode = STDBY_RC);

  // Instantaneous RSSI of whatever is currently on the tuned frequency, in dBm.
  // Valid only while in RX. More negative == quieter.
  float readRssiDbm();

  // Optional ~+3 dB sensitivity boost (high-gain LNA mode). Good for monitoring.
  void enableHighSensitivity();

  uint8_t getStatus();

private:
  int8_t _nss, _rst, _busy;

  void    reset();
  void    waitBusy(uint32_t timeoutUs = 5000);
  void    writeCommand(uint8_t opcode, const uint8_t *params, uint8_t n);
  void    readCommand(uint8_t opcode, uint8_t *result, uint8_t n);
  void    writeRegister(uint16_t addr, uint8_t value);
  uint8_t readRegister(uint16_t addr);
};
