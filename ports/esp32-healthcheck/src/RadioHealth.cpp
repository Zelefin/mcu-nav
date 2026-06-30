#include <Arduino.h>
#include <RadioLib.h>
#include <SPI.h>

#include "BoardPins.h"
#include "HealthStatus.h"
#include "Logger.h"

namespace {
SX1280 radio = new Module(PIN_LORA_CS, PIN_LORA_DIO1, PIN_LORA_RST, PIN_LORA_BUSY);
volatile bool gPacketReceived = false;

#if defined(ESP32)
IRAM_ATTR
#endif
void setRadioPacketReceivedFlag() {
  gPacketReceived = true;
}

bool waitBusyLow(uint32_t timeoutMs) {
  const uint32_t startMs = millis();
  while (digitalRead(PIN_LORA_BUSY) == HIGH) {
    if ((millis() - startMs) >= timeoutMs) {
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  return true;
}

void publishRadioStatus(const RadioHealthStatus &status) {
  HealthStatus::setRadio(status);
}

HealthState radioStateFor(const RadioHealthStatus &status) {
  if (!status.initialized || status.lastError != RADIOLIB_ERR_NONE) {
    return HealthState::Fail;
  }
  if (status.txOk && status.rxOk) {
    return HealthState::Ok;
  }
  return HealthState::Warn;
}
}  // namespace

extern "C" void RadioHealthTask(void *) {
  RadioHealthStatus status;

  Logger::infof("RADIO", "Initializing SX128x radio over SPI");
  Logger::infof("RADIO",
                "SPI pins: SCK=%d MISO=%d MOSI=%d CS=%d RST=%d BUSY=%d DIO1=%d",
                BoardPins::loraSck,
                BoardPins::loraMiso,
                BoardPins::loraMosi,
                BoardPins::loraCs,
                BoardPins::loraRst,
                BoardPins::loraBusy,
                BoardPins::loraDio1);

  pinMode(PIN_LORA_BUSY, INPUT);
  pinMode(PIN_LORA_DIO1, INPUT);
  SPI.begin(PIN_LORA_SCK, PIN_LORA_MISO, PIN_LORA_MOSI, PIN_LORA_CS);

  if (!waitBusyLow(1000)) {
    status.state = HealthState::Fail;
    status.lastError = RADIOLIB_ERR_SPI_CMD_TIMEOUT;
    publishRadioStatus(status);
    Logger::failf("RADIO", "BUSY pin stayed HIGH before init");
    vTaskDelete(nullptr);
  }

  int state = radio.begin(RADIO_FREQUENCY_MHZ,
                          RADIO_BANDWIDTH_KHZ,
                          RADIO_SPREADING_FACTOR,
                          RADIO_CODING_RATE,
                          RADIO_SYNC_WORD,
                          RADIO_TX_POWER_DBM,
                          RADIO_PREAMBLE_LEN);
  status.lastError = state;
  if (state != RADIOLIB_ERR_NONE) {
    status.state = HealthState::Fail;
    publishRadioStatus(status);
    Logger::failf("RADIO", "SX128x init failed, code=%d", state);
    vTaskDelete(nullptr);
  }

  status.initialized = true;
  status.state = HealthState::Warn;
  publishRadioStatus(status);
  Logger::okf("RADIO",
              "SX128x initialized: freq=%.1f MHz bw=%.1f kHz sf=%d cr=%d power=%d dBm",
              static_cast<double>(RADIO_FREQUENCY_MHZ),
              static_cast<double>(RADIO_BANDWIDTH_KHZ),
              RADIO_SPREADING_FACTOR,
              RADIO_CODING_RATE,
              RADIO_TX_POWER_DBM);

  radio.setPacketReceivedAction(setRadioPacketReceivedFlag);
  state = radio.startReceive();
  if (state == RADIOLIB_ERR_NONE) {
    Logger::okf("RADIO", "RX listen mode started");
  } else {
    status.lastError = state;
    status.state = HealthState::Warn;
    publishRadioStatus(status);
    Logger::warnf("RADIO", "RX listen mode failed to start, code=%d", state);
  }

  uint32_t packetSeq = 0;
  uint32_t lastTxMs = millis();

  for (;;) {
    if (gPacketReceived) {
      gPacketReceived = false;

      String received;
      state = radio.readData(received);
      if (state == RADIOLIB_ERR_NONE) {
        status.rxOk = true;
        status.rxOkCount++;
        status.lastError = RADIOLIB_ERR_NONE;
        status.lastRssiDbm = radio.getRSSI();
        status.lastSnrDb = radio.getSNR();
        status.state = radioStateFor(status);
        publishRadioStatus(status);
        Logger::okf("RADIO",
                    "RX packet received: bytes=%u RSSI=%.1f dBm SNR=%.1f dB payload=\"%s\"",
                    received.length(),
                    static_cast<double>(status.lastRssiDbm),
                    static_cast<double>(status.lastSnrDb),
                    received.c_str());
      } else if (state == RADIOLIB_ERR_CRC_MISMATCH) {
        status.lastError = state;
        status.state = HealthState::Warn;
        publishRadioStatus(status);
        Logger::warnf("RADIO", "RX CRC mismatch");
      } else {
        status.lastError = state;
        status.state = HealthState::Warn;
        publishRadioStatus(status);
        Logger::warnf("RADIO", "RX read failed, code=%d", state);
      }

      state = radio.startReceive();
      if (state != RADIOLIB_ERR_NONE) {
        status.lastError = state;
        status.state = HealthState::Warn;
        publishRadioStatus(status);
        Logger::warnf("RADIO", "RX restart failed, code=%d", state);
      }
    }

    const uint32_t nowMs = millis();
    if ((nowMs - lastTxMs) >= RADIO_TX_INTERVAL_MS) {
      lastTxMs = nowMs;
      String payload = "mcu-nav-health board=" + String(BoardPins::boardName) +
                       " seq=" + String(packetSeq++);

      Logger::debugf("RADIO", "BUSY=%d before TX", digitalRead(PIN_LORA_BUSY));
      if (!waitBusyLow(1000)) {
        status.lastError = RADIOLIB_ERR_SPI_CMD_TIMEOUT;
        status.state = HealthState::Fail;
        publishRadioStatus(status);
        Logger::failf("RADIO", "BUSY pin stayed HIGH before TX");
      } else {
        state = radio.transmit(payload);
        status.lastError = state;
        if (state == RADIOLIB_ERR_NONE) {
          status.txOk = true;
          status.txOkCount++;
          status.state = radioStateFor(status);
          publishRadioStatus(status);
          Logger::okf("RADIO", "TX packet sent: bytes=%u payload=\"%s\"", payload.length(), payload.c_str());
        } else {
          status.state = HealthState::Fail;
          publishRadioStatus(status);
          Logger::failf("RADIO", "TX failed, code=%d", state);
        }
      }

      state = radio.startReceive();
      if (state != RADIOLIB_ERR_NONE) {
        status.lastError = state;
        status.state = HealthState::Warn;
        publishRadioStatus(status);
        Logger::warnf("RADIO", "RX listen mode failed after TX, code=%d", state);
      }
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
