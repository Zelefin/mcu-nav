// ---------------------------------------------------------------------------
// Minimal SX1280 ranging driver for SpeedyBee Nano 2.4G bring-up.
//
// This is intentionally local to examples/sb24tx-ranging. It only exposes the
// SX1280 commands needed to prove the ranging engine on ESP8285 hardware.
// ---------------------------------------------------------------------------
#pragma once

#include <Arduino.h>

class SX1280 {
public:
  enum Opcode : uint8_t {
    OP_GET_STATUS = 0xC0,
    OP_WRITE_REGISTER = 0x18,
    OP_READ_REGISTER = 0x19,
    OP_WRITE_BUFFER = 0x1A,
    OP_READ_BUFFER = 0x1B,
    OP_SET_STANDBY = 0x80,
    OP_SET_RX = 0x82,
    OP_SET_TX = 0x83,
    OP_SET_PACKET_TYPE = 0x8A,
    OP_GET_PACKET_TYPE = 0x03,
    OP_SET_RF_FREQUENCY = 0x86,
    OP_SET_TX_PARAMS = 0x8E,
    OP_SET_BUFFER_BASE_ADDRESS = 0x8F,
    OP_SET_MODULATION_PARAMS = 0x8B,
    OP_SET_PACKET_PARAMS = 0x8C,
    OP_GET_RX_BUFFER_STATUS = 0x17,
    OP_SET_DIO_IRQ_PARAMS = 0x8D,
    OP_GET_IRQ_STATUS = 0x15,
    OP_CLEAR_IRQ_STATUS = 0x97,
    OP_GET_PACKET_STATUS = 0x1D,
    OP_SET_REGULATOR_MODE = 0x96,
    OP_SET_RANGING_ROLE = 0xA3,
  };

  enum PacketType : uint8_t {
    PACKET_TYPE_LORA = 0x01,
    PACKET_TYPE_RANGING = 0x02,
  };

  enum Standby : uint8_t {
    STDBY_RC = 0x00,
    STDBY_XOSC = 0x01,
  };

  enum RangingRole : uint8_t {
    RANGING_ROLE_SLAVE = 0x00,
    RANGING_ROLE_MASTER = 0x01,
  };

  enum Irq : uint16_t {
    IRQ_TX_DONE = 1u << 0,
    IRQ_RX_DONE = 1u << 1,
    IRQ_SYNC_WORD_VALID = 1u << 2,
    IRQ_HEADER_VALID = 1u << 4,
    IRQ_HEADER_ERROR = 1u << 5,
    IRQ_CRC_ERROR = 1u << 6,
    IRQ_RANGING_SLAVE_RESPONSE_DONE = 1u << 7,
    IRQ_RANGING_SLAVE_REQUEST_DISCARD = 1u << 8,
    IRQ_RANGING_MASTER_RESULT_VALID = 1u << 9,
    IRQ_RANGING_MASTER_TIMEOUT = 1u << 10,
    IRQ_RANGING_SLAVE_REQUEST_VALID = 1u << 11,
    IRQ_RX_TX_TIMEOUT = 1u << 14,
    IRQ_ALL = 0xFFFF,
  };

  struct PacketStatus {
    int16_t rssi_dbm;
    float snr_db;
  };

  SX1280(int8_t nss, int8_t rst, int8_t busy);

  bool beginLoRa(uint32_t frequency_hz);
  bool beginRanging(RangingRole role, uint32_t frequency_hz,
                    uint32_t ranging_address, uint16_t calibration);

  bool transmitPacket(const uint8_t *payload, uint8_t len, uint32_t timeout_ms);
  bool receivePacket(uint8_t *payload, uint8_t max_len, uint8_t *out_len,
                     uint32_t timeout_ms, PacketStatus *out_status);

  void startMasterExchange(uint16_t timeout_ms);
  void startSlaveListen();

  uint16_t getIrqStatus();
  void clearIrqStatus(uint16_t irq_mask = IRQ_ALL);

  int32_t readRangingResultRaw();
  PacketStatus readPacketStatus();
  float rawRangingMeters(int32_t raw_result) const;

  void setStandby(Standby mode = STDBY_RC);
  uint8_t getStatus();
  uint8_t getPacketType();

private:
  int8_t _nss, _rst, _busy;

  void reset();
  bool waitBusy(uint32_t timeout_us = 10000);
  void writeCommand(uint8_t opcode, const uint8_t *params, uint8_t n);
  void readCommand(uint8_t opcode, uint8_t *result, uint8_t n);
  void writeRegister(uint16_t addr, uint8_t value);
  void writeRegister(uint16_t addr, const uint8_t *values, uint8_t n);
  uint8_t readRegister(uint16_t addr);
  void writeBuffer(const uint8_t *data, uint8_t n, uint8_t offset = 0);
  void readBuffer(uint8_t *data, uint8_t n, uint8_t offset = 0);
  void setFrequencyHz(uint32_t freq_hz);
  void setLoRaSyncWord(uint8_t sync_word);
  void setPacketType(PacketType type);
  void setBufferBaseAddress(uint8_t tx_base = 0x00, uint8_t rx_base = 0x80);
  void setModulationParams(uint8_t p1, uint8_t p2, uint8_t p3);
  void setPacketParamsLoRa(uint8_t payload_len);
  void setTxParams(uint8_t power_dbm);
  void setDioIrqParams(uint16_t irq_mask, uint16_t dio1_mask,
                       uint16_t dio2_mask, uint16_t dio3_mask);
  void setRangingAddress(RangingRole role, uint32_t address);
  void setRangingCalibration(uint16_t calibration);
};
