#include "BoardPins.h"
#include "EspIdfRadioLibHal.h"
#include "HealthStatus.h"
#include "Logger.h"

#include <RadioLib.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
EspIdfRadioLibHal radioHal(PIN_LORA_SCK, PIN_LORA_MISO, PIN_LORA_MOSI);
Module radioModule(&radioHal, PIN_LORA_CS, PIN_LORA_DIO1, PIN_LORA_RST, PIN_LORA_BUSY);
SX1280 radio(&radioModule);

volatile bool gPacketReceived = false;

uint32_t nowMs() {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

void IRAM_ATTR setRadioPacketReceivedFlag() {
  gPacketReceived = true;
}

bool waitBusyLow(uint32_t timeoutMs) {
  const uint32_t startMs = nowMs();
  while (gpio_get_level(static_cast<gpio_num_t>(PIN_LORA_BUSY)) != 0) {
    if ((nowMs() - startMs) >= timeoutMs) {
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

  gpio_config_t inputConfig = {};
  inputConfig.pin_bit_mask = (1ULL << PIN_LORA_BUSY) | (1ULL << PIN_LORA_DIO1);
  inputConfig.mode = GPIO_MODE_INPUT;
  inputConfig.pull_up_en = GPIO_PULLUP_DISABLE;
  inputConfig.pull_down_en = GPIO_PULLDOWN_DISABLE;
  inputConfig.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&inputConfig);

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
  uint32_t lastTxMs = nowMs();

  for (;;) {
    if (gPacketReceived) {
      gPacketReceived = false;

      uint8_t received[128] = {};
      const size_t packetLength = radio.getPacketLength(true);
      const size_t readLength = packetLength < (sizeof(received) - 1) ? packetLength : (sizeof(received) - 1);
      state = radio.readData(received, readLength);
      if (state == RADIOLIB_ERR_NONE) {
        received[readLength] = '\0';
        status.rxOk = true;
        status.rxOkCount++;
        status.lastError = RADIOLIB_ERR_NONE;
        status.lastRssiDbm = radio.getRSSI();
        status.lastSnrDb = radio.getSNR();
        status.state = radioStateFor(status);
        publishRadioStatus(status);
        Logger::okf("RADIO",
                    "RX packet received: bytes=%u RSSI=%.1f dBm SNR=%.1f dB payload=\"%s\"",
                    static_cast<unsigned>(packetLength),
                    static_cast<double>(status.lastRssiDbm),
                    static_cast<double>(status.lastSnrDb),
                    reinterpret_cast<const char *>(received));
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

    const uint32_t currentMs = nowMs();
    if ((currentMs - lastTxMs) >= RADIO_TX_INTERVAL_MS) {
      lastTxMs = currentMs;

      char payload[96];
      const int payloadLength = snprintf(payload,
                                         sizeof(payload),
                                         "mcu-nav-health board=%s seq=%lu",
                                         BoardPins::boardName,
                                         static_cast<unsigned long>(packetSeq++));
      if (payloadLength < 0) {
        status.lastError = RADIOLIB_ERR_UNKNOWN;
        status.state = HealthState::Fail;
        publishRadioStatus(status);
        Logger::failf("RADIO", "TX payload formatting failed");
        vTaskDelay(pdMS_TO_TICKS(20));
        continue;
      }

      Logger::debugf("RADIO", "BUSY=%d before TX", gpio_get_level(static_cast<gpio_num_t>(PIN_LORA_BUSY)));
      if (!waitBusyLow(1000)) {
        status.lastError = RADIOLIB_ERR_SPI_CMD_TIMEOUT;
        status.state = HealthState::Fail;
        publishRadioStatus(status);
        Logger::failf("RADIO", "BUSY pin stayed HIGH before TX");
      } else {
        const size_t txLength = static_cast<size_t>(payloadLength) < sizeof(payload) ? static_cast<size_t>(payloadLength) : sizeof(payload) - 1;
        state = radio.transmit(reinterpret_cast<const uint8_t *>(payload), txLength);
        status.lastError = state;
        if (state == RADIOLIB_ERR_NONE) {
          status.txOk = true;
          status.txOkCount++;
          status.state = radioStateFor(status);
          publishRadioStatus(status);
          Logger::okf("RADIO", "TX packet sent: bytes=%u payload=\"%s\"", static_cast<unsigned>(txLength), payload);
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
