#include "BoardPins.h"
#include "ControlChannel.h"
#include "EspIdfRadioLibHal.h"
#include "HealthStatus.h"
#include "Logger.h"

#include <RadioLib.h>

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>

#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nav/nav_types.h"

namespace {
constexpr float kRangingFrequencyMhz = 2445.0f;
constexpr float kRangingBandwidthKhz = 1625.0f;
constexpr uint8_t kRangingSpreadingFactor = 7u;
constexpr uint8_t kRangingCodingRate = 5u;
constexpr uint8_t kRangingSyncWord = RADIOLIB_SX128X_SYNC_WORD_PRIVATE;
constexpr int8_t kRangingTxPowerDbm = 2;
constexpr uint16_t kRangingPreambleLen = 12u;
constexpr uint32_t kRangingAddressBase = 0x4E415600UL;  // "NAV" + slave id
constexpr uint32_t kMasterTimeoutMs = 350u;
constexpr uint32_t kMasterPeerGapMs = 120u;
constexpr uint32_t kSlaveListenWindowMs = 250u;
constexpr uint32_t kSlaveListenSliceMs = 120u;
constexpr uint32_t kReportRxWindowMs = 45u;
constexpr uint8_t kReportTxRepeats = 3u;
constexpr uint32_t kReportTxGapMs = 8u;
constexpr uint32_t kMasterScanIntervalMs = 3200u;
constexpr uint32_t kInitialMasterDelayMs = 1000u;
constexpr uint32_t kMasterTurnSpacingMs = 700u;
constexpr uint32_t kConfigPollMs = 250u;
constexpr uint32_t kRangeSigmaMm = 1000u;
constexpr size_t kRangeReportPayloadMax = 224u;

uint16_t gRangingCalibration[3][6] = {
    {10299, 10271, 10244, 10242, 10230, 10246},
    {11486, 11474, 11453, 11426, 11417, 11401},
    {13308, 13493, 13528, 13515, 13430, 13376},
};

EspIdfRadioLibHal radioHal(PIN_LORA_SCK, PIN_LORA_MISO, PIN_LORA_MOSI);
Module radioModule(&radioHal, PIN_LORA_CS, PIN_LORA_DIO1, PIN_LORA_RST, PIN_LORA_BUSY);
SX1280 radio(&radioModule);

RadioHealthStatus gStatus;
uint16_t gRequestId = 0u;

uint32_t nowMs() { return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL); }

bool isValidNodeId(uint8_t nodeId) { return nodeId < NAV_MAX_NODES; }

uint32_t rangingAddressFor(uint8_t slaveNodeId) {
  return kRangingAddressBase | static_cast<uint32_t>(slaveNodeId);
}

bool timeReached(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

uint8_t nextPeerAfter(uint8_t selfId, uint8_t previousPeerId) {
  for (uint8_t offset = 1u; offset <= NAV_MAX_NODES; ++offset) {
    const uint8_t candidate = static_cast<uint8_t>((previousPeerId + offset) % NAV_MAX_NODES);
    if (candidate != selfId) {
      return candidate;
    }
  }
  return NAV_INVALID_NODE_ID;
}

uint32_t initialMasterAt(uint8_t nodeId) {
  return nowMs() + kInitialMasterDelayMs + (static_cast<uint32_t>(nodeId) * kMasterTurnSpacingMs);
}

uint32_t listenWindowUntil(uint32_t now, uint32_t deadline) {
  if (timeReached(now, deadline)) {
    return 0u;
  }
  const uint32_t untilDeadlineMs = deadline - now;
  return untilDeadlineMs < kSlaveListenWindowMs ? untilDeadlineMs : kSlaveListenWindowMs;
}

int readPin(int pin) {
  if (pin < 0 || pin >= GPIO_NUM_MAX) {
    return 0;
  }
  return gpio_get_level(static_cast<gpio_num_t>(pin));
}

void configureInputPin(int pin) {
  if (pin < 0 || pin >= GPIO_NUM_MAX) {
    return;
  }
  gpio_config_t config = {};
  config.pin_bit_mask = (1ULL << pin);
  config.mode = GPIO_MODE_INPUT;
  config.pull_up_en = GPIO_PULLUP_DISABLE;
  config.pull_down_en = GPIO_PULLDOWN_DISABLE;
  config.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&config);
}

void delayMs(uint32_t ms) {
  TickType_t ticks = pdMS_TO_TICKS(ms);
  if (ticks == 0) {
    ticks = 1;
  }
  vTaskDelay(ticks);
}

const char *rangeFailReasonName(nav_range_fail_reason_t reason) {
  switch (reason) {
    case NAV_RANGE_FAIL_NONE:
      return "NONE";
    case NAV_RANGE_FAIL_TIMEOUT:
      return "TIMEOUT";
    case NAV_RANGE_FAIL_NO_RESPONSE:
      return "NO_RESPONSE";
    case NAV_RANGE_FAIL_RADIO_BUSY:
      return "RADIO_BUSY";
    case NAV_RANGE_FAIL_BAD_FRAME:
      return "BAD_FRAME";
    case NAV_RANGE_FAIL_RANGING_ENGINE_ERROR:
      return "RANGING_ENGINE_ERROR";
    case NAV_RANGE_FAIL_ABORTED:
      return "ABORTED";
    case NAV_RANGE_FAIL_UNKNOWN:
    default:
      return "UNKNOWN";
  }
}

void sanitizeReportText(char *text) {
  if (!text) {
    return;
  }
  for (char *p = text; *p != '\0'; ++p) {
    const unsigned char c = static_cast<unsigned char>(*p);
    if (c < 32u || c > 126u) {
      *p = ' ';
    }
  }
}

bool waitBusyLow(uint32_t timeoutMs) {
  const uint32_t startedMs = nowMs();
  while (readPin(PIN_LORA_BUSY) != 0) {
    if ((nowMs() - startedMs) >= timeoutMs) {
      return false;
    }
    delayMs(2);
  }
  return true;
}

bool waitDio1High(uint32_t timeoutMs) {
  const uint32_t startedMs = nowMs();
  while (readPin(PIN_LORA_DIO1) == 0) {
    if ((nowMs() - startedMs) >= timeoutMs) {
      return false;
    }
    delayMs(2);
  }
  return true;
}

void broadcastRangeReport(const char *rangeResultText) {
  if (!rangeResultText || rangeResultText[0] == '\0') {
    return;
  }

  char payload[kRangeReportPayloadMax + 1u];
  const int written = snprintf(payload, sizeof(payload), "NRR1 %s", rangeResultText);
  if (written <= 0 || static_cast<size_t>(written) >= sizeof(payload)) {
    Logger::warnf("RANGE", "range_report tx=false note=\"payload too long\"");
    return;
  }

  const size_t payloadLen = strlen(payload);
  for (uint8_t attempt = 0; attempt < kReportTxRepeats; ++attempt) {
    if (!waitBusyLow(100)) {
      Logger::warnf("RANGE", "range_report tx=false error=%d note=\"BUSY stayed high\"", RADIOLIB_ERR_SPI_CMD_TIMEOUT);
      return;
    }
    const int16_t state = radio.transmit(reinterpret_cast<const uint8_t *>(payload), payloadLen);
    if (state != RADIOLIB_ERR_NONE) {
      Logger::warnf("RANGE", "range_report tx=false error=%d attempt=%u", state, static_cast<unsigned>(attempt + 1u));
      return;
    }
    if ((attempt + 1u) < kReportTxRepeats) {
      delayMs(kReportTxGapMs);
    }
  }
}

void serviceReportRx(uint8_t nodeId, uint32_t windowMs) {
  if (windowMs < 5u) {
    return;
  }
  if (!waitBusyLow(100)) {
    return;
  }

  (void)radio.clearIrqFlags(RADIOLIB_SX128X_IRQ_ALL);
  const int16_t startState = radio.startReceive();
  if (startState != RADIOLIB_ERR_NONE) {
    return;
  }

  if (!waitDio1High(windowMs)) {
    (void)radio.finishReceive();
    return;
  }

  const size_t packetLen = radio.getPacketLength();
  char payload[kRangeReportPayloadMax + 1u] = {};
  const size_t readLen = packetLen < kRangeReportPayloadMax ? packetLen : kRangeReportPayloadMax;
  const int16_t state = radio.readData(reinterpret_cast<uint8_t *>(payload), readLen);
  payload[readLen] = '\0';
  sanitizeReportText(payload);

  if (state != RADIOLIB_ERR_NONE || strncmp(payload, "NRR1 ", 5u) != 0) {
    return;
  }

  const float reportRssi = radio.getRSSI();
  const float reportSnr = radio.getSNR();
  Logger::infof("RANGE",
                "%s source=air_report heard_by=%u report_rssi_dbm=%.1f report_snr_db=%.1f",
                payload + 5u,
                static_cast<unsigned>(nodeId),
                static_cast<double>(reportRssi),
                static_cast<double>(reportSnr));
}

void publishStatus(HealthState state) {
  gStatus.state = state;
  HealthStatus::setRadio(gStatus);
}

void injectRangeResult(uint8_t peerId,
                       uint16_t requestId,
                       uint32_t rangeMm,
                       int16_t rssiDbm,
                       int16_t snrDb,
                       bool valid) {
  nav_event_t event = {};
  event.type = NAV_EVT_RANGE_RESULT;
  event.timestamp_ms = nowMs();
  event.data.range_result.peer_id = peerId;
  event.data.range_result.request_id = requestId;
  event.data.range_result.timestamp_ms = event.timestamp_ms;
  event.data.range_result.range_mm = rangeMm;
  event.data.range_result.range_sigma_mm = kRangeSigmaMm;
  event.data.range_result.rssi_dbm = rssiDbm;
  event.data.range_result.snr_db = snrDb;
  event.data.range_result.valid = valid;
  (void)ControlChannel::handleEvent(&event);
}

void injectRangeFail(uint8_t peerId, uint16_t requestId, nav_range_fail_reason_t reason) {
  nav_event_t event = {};
  event.type = NAV_EVT_RANGE_FAIL;
  event.timestamp_ms = nowMs();
  event.data.range_failure.peer_id = peerId;
  event.data.range_failure.request_id = requestId;
  event.data.range_failure.timestamp_ms = event.timestamp_ms;
  event.data.range_failure.reason = reason;
  (void)ControlChannel::handleEvent(&event);
}

void logIrqFlags(uint16_t irq, char *buffer, size_t bufferSize) {
  if (bufferSize == 0u) {
    return;
  }
  buffer[0] = '\0';
  auto append = [&](const char *flag) {
    const size_t used = strlen(buffer);
    if (used + 2u >= bufferSize) {
      return;
    }
    if (used > 0u) {
      strncat(buffer, " ", bufferSize - strlen(buffer) - 1u);
    }
    strncat(buffer, flag, bufferSize - strlen(buffer) - 1u);
  };
  if (irq & RADIOLIB_SX128X_IRQ_RANGING_SLAVE_REQ_VALID) append("slave_req_valid");
  if (irq & RADIOLIB_SX128X_IRQ_RANGING_MASTER_TIMEOUT) append("master_timeout");
  if (irq & RADIOLIB_SX128X_IRQ_RANGING_MASTER_RES_VALID) append("master_result_valid");
  if (irq & RADIOLIB_SX128X_IRQ_RANGING_SLAVE_REQ_DISCARD) append("slave_req_discard");
  if (irq & RADIOLIB_SX128X_IRQ_RANGING_SLAVE_RESP_DONE) append("slave_response_done");
  if (irq & RADIOLIB_SX128X_IRQ_RX_TX_TIMEOUT) append("rx_tx_timeout");
}

void logMasterFailure(uint8_t masterId,
                      uint8_t peerId,
                      uint16_t requestId,
                      uint32_t elapsedMs,
                      int16_t error,
                      nav_range_fail_reason_t reason,
                      const char *note) {
  const uint16_t irq = radio.getIrqStatus();
  char flags[96];
  logIrqFlags(irq, flags, sizeof(flags));
  Logger::warnf("RANGE",
                "range_result ok=false from=%u to=%u request_id=%u range_fail_reason=%s elapsed_ms=%lu error=%d irq=0x%04X flags=\"%s\" note=\"%s\"",
                static_cast<unsigned>(masterId),
                static_cast<unsigned>(peerId),
                static_cast<unsigned>(requestId),
                rangeFailReasonName(reason),
                static_cast<unsigned long>(elapsedMs),
                error,
                irq,
                flags,
                note);
  char report[kRangeReportPayloadMax];
  snprintf(report,
           sizeof(report),
           "range_result ok=false from=%u to=%u request_id=%u range_fail_reason=%s elapsed_ms=%lu error=%d note=\"%s\"",
           static_cast<unsigned>(masterId),
           static_cast<unsigned>(peerId),
           static_cast<unsigned>(requestId),
           rangeFailReasonName(reason),
           static_cast<unsigned long>(elapsedMs),
           error,
           note);
  broadcastRangeReport(report);
}

void runMasterExchange(uint8_t masterId, uint8_t peerId) {
  const uint16_t requestId = ++gRequestId;
  const uint32_t startedMs = nowMs();

  gStatus.txOk = true;
  gStatus.txOkCount++;
  publishStatus(HealthState::Warn);

  if (!waitBusyLow(1000)) {
    logMasterFailure(masterId,
                     peerId,
                     requestId,
                     nowMs() - startedMs,
                     RADIOLIB_ERR_SPI_CMD_TIMEOUT,
                     NAV_RANGE_FAIL_RADIO_BUSY,
                     "BUSY stayed high before ranging");
    injectRangeFail(peerId, requestId, NAV_RANGE_FAIL_RADIO_BUSY);
    return;
  }

  (void)radio.clearIrqFlags(RADIOLIB_SX128X_IRQ_ALL);
  int16_t state = radio.startRanging(true, rangingAddressFor(peerId), gRangingCalibration);
  if (state != RADIOLIB_ERR_NONE) {
    (void)radio.finishRanging();
    logMasterFailure(masterId,
                     peerId,
                     requestId,
                     nowMs() - startedMs,
                     state,
                     NAV_RANGE_FAIL_RANGING_ENGINE_ERROR,
                     "startRanging master failed");
    injectRangeFail(peerId, requestId, NAV_RANGE_FAIL_RANGING_ENGINE_ERROR);
    return;
  }

  if (!waitDio1High(kMasterTimeoutMs)) {
    (void)radio.finishRanging();
    logMasterFailure(masterId,
                     peerId,
                     requestId,
                     nowMs() - startedMs,
                     RADIOLIB_ERR_RANGING_TIMEOUT,
                     NAV_RANGE_FAIL_TIMEOUT,
                     "ranging timeout");
    injectRangeFail(peerId, requestId, NAV_RANGE_FAIL_TIMEOUT);
    return;
  }

  const uint16_t irq = radio.getIrqStatus();
  state = radio.finishRanging();
  if (state != RADIOLIB_ERR_NONE) {
    logMasterFailure(masterId,
                     peerId,
                     requestId,
                     nowMs() - startedMs,
                     state,
                     NAV_RANGE_FAIL_RANGING_ENGINE_ERROR,
                     "finishRanging master failed");
    injectRangeFail(peerId, requestId, NAV_RANGE_FAIL_RANGING_ENGINE_ERROR);
    return;
  }

  const int32_t rawReg = radio.getRangingResultRaw();
  const float rangeM = radio.getRangingResult();
  const float rssi = radio.getRSSI();
  const float snr = radio.getSNR();
  const bool valid = std::isfinite(rangeM) && rangeM > 0.0f;
  const uint32_t rangeMm = valid ? static_cast<uint32_t>(std::lround(rangeM * 1000.0f)) : 0u;
  const uint32_t elapsedMs = nowMs() - startedMs;
  char flags[96];
  logIrqFlags(irq, flags, sizeof(flags));

  if (!valid) {
    Logger::warnf("RANGE",
                  "range_result ok=false from=%u to=%u request_id=%u range_fail_reason=%s raw_reg=%ld uncorrected_m=%.2f elapsed_ms=%lu irq=0x%04X flags=\"%s\" note=\"invalid distance\"",
                  static_cast<unsigned>(masterId),
                  static_cast<unsigned>(peerId),
                  static_cast<unsigned>(requestId),
                  rangeFailReasonName(NAV_RANGE_FAIL_RANGING_ENGINE_ERROR),
                  static_cast<long>(rawReg),
                  static_cast<double>(rangeM),
                  static_cast<unsigned long>(elapsedMs),
                  irq,
                  flags);
    char report[kRangeReportPayloadMax];
    snprintf(report,
             sizeof(report),
             "range_result ok=false from=%u to=%u request_id=%u range_fail_reason=%s raw_reg=%ld uncorrected_m=%.2f elapsed_ms=%lu note=\"invalid distance\"",
             static_cast<unsigned>(masterId),
             static_cast<unsigned>(peerId),
             static_cast<unsigned>(requestId),
             rangeFailReasonName(NAV_RANGE_FAIL_RANGING_ENGINE_ERROR),
             static_cast<long>(rawReg),
             static_cast<double>(rangeM),
             static_cast<unsigned long>(elapsedMs));
    broadcastRangeReport(report);
    injectRangeFail(peerId, requestId, NAV_RANGE_FAIL_RANGING_ENGINE_ERROR);
    return;
  }

  gStatus.rxOk = true;
  gStatus.rxOkCount++;
  gStatus.lastError = RADIOLIB_ERR_NONE;
  gStatus.lastRssiDbm = rssi;
  gStatus.lastSnrDb = snr;
  publishStatus(HealthState::Ok);

  Logger::okf("RANGE",
              "range_result ok=true from=%u to=%u request_id=%u range_mm=%lu uncorrected_m=%.2f raw_reg=%ld rssi_dbm=%.1f snr_db=%.1f elapsed_ms=%lu irq=0x%04X flags=\"%s\"",
              static_cast<unsigned>(masterId),
              static_cast<unsigned>(peerId),
              static_cast<unsigned>(requestId),
              static_cast<unsigned long>(rangeMm),
              static_cast<double>(rangeM),
              static_cast<long>(rawReg),
              static_cast<double>(rssi),
              static_cast<double>(snr),
              static_cast<unsigned long>(elapsedMs),
              irq,
              flags);
  char report[kRangeReportPayloadMax];
  snprintf(report,
           sizeof(report),
           "range_result ok=true from=%u to=%u request_id=%u range_mm=%lu uncorrected_m=%.2f raw_reg=%ld rssi_dbm=%.1f snr_db=%.1f elapsed_ms=%lu note=\"range ok\"",
           static_cast<unsigned>(masterId),
           static_cast<unsigned>(peerId),
           static_cast<unsigned>(requestId),
           static_cast<unsigned long>(rangeMm),
           static_cast<double>(rangeM),
           static_cast<long>(rawReg),
           static_cast<double>(rssi),
           static_cast<double>(snr),
           static_cast<unsigned long>(elapsedMs));
  broadcastRangeReport(report);
  injectRangeResult(peerId,
                    requestId,
                    rangeMm,
                    static_cast<int16_t>(std::lround(rssi)),
                    static_cast<int16_t>(std::lround(snr)),
                    true);
}

void serviceSlave(uint8_t nodeId, uint32_t listenWindowMs) {
  const uint32_t startedMs = nowMs();
  const uint32_t address = rangingAddressFor(nodeId);

  if (!waitBusyLow(1000)) {
    gStatus.lastError = RADIOLIB_ERR_SPI_CMD_TIMEOUT;
    publishStatus(HealthState::Fail);
    Logger::warnf("RANGE", "slave_listen ok=false node=%u error=%d note=\"BUSY stayed high\"", static_cast<unsigned>(nodeId), gStatus.lastError);
    delayMs(500);
    return;
  }

  (void)radio.clearIrqFlags(RADIOLIB_SX128X_IRQ_ALL);
  int16_t state = radio.startRanging(false, address, gRangingCalibration);
  if (state != RADIOLIB_ERR_NONE) {
    gStatus.lastError = state;
    publishStatus(HealthState::Warn);
    Logger::warnf("RANGE", "slave_listen ok=false node=%u address=0x%08lX error=%d", static_cast<unsigned>(nodeId), static_cast<unsigned long>(address), state);
    delayMs(500);
    return;
  }

  while (readPin(PIN_LORA_DIO1) == 0 && (nowMs() - startedMs) < listenWindowMs) {
    delayMs(20);
  }

  const uint16_t irq = radio.getIrqStatus();
  if (readPin(PIN_LORA_DIO1) == 0) {
    (void)radio.finishRanging();
    publishStatus(HealthState::Warn);
    return;
  }

  state = radio.finishRanging();
  char flags[96];
  logIrqFlags(irq, flags, sizeof(flags));
  gStatus.lastError = state;
  if (state == RADIOLIB_ERR_NONE) {
    gStatus.txOk = true;
    gStatus.rxOk = true;
    gStatus.txOkCount++;
    gStatus.rxOkCount++;
    publishStatus(HealthState::Ok);
    Logger::okf("RANGE",
                "slave_response ok=true node=%u address=0x%08lX elapsed_ms=%lu irq=0x%04X flags=\"%s\"",
                static_cast<unsigned>(nodeId),
                static_cast<unsigned long>(address),
                static_cast<unsigned long>(nowMs() - startedMs),
                irq,
                flags);
  } else {
    publishStatus(HealthState::Warn);
    Logger::warnf("RANGE",
                  "slave_response ok=false node=%u address=0x%08lX error=%d elapsed_ms=%lu irq=0x%04X flags=\"%s\"",
                  static_cast<unsigned>(nodeId),
                  static_cast<unsigned long>(address),
                  state,
                  static_cast<unsigned long>(nowMs() - startedMs),
                  irq,
                  flags);
  }
}

void serviceNetworkListen(uint8_t nodeId, uint32_t listenWindowMs) {
  const uint32_t startedMs = nowMs();
  while ((nowMs() - startedMs) < listenWindowMs) {
    const uint32_t elapsedMs = nowMs() - startedMs;
    uint32_t remainingMs = listenWindowMs - elapsedMs;
    if (remainingMs >= (kReportRxWindowMs + 20u)) {
      serviceReportRx(nodeId, kReportRxWindowMs);
      remainingMs = listenWindowMs - (nowMs() - startedMs);
    }
    if (remainingMs < 20u) {
      break;
    }
    const uint32_t slaveWindowMs = remainingMs < kSlaveListenSliceMs ? remainingMs : kSlaveListenSliceMs;
    serviceSlave(nodeId, slaveWindowMs);
  }
}

void initRadio() {
  configureInputPin(PIN_LORA_BUSY);
  configureInputPin(PIN_LORA_DIO1);

  Logger::infof("RADIO", "Initializing SX128x distance-only ranging");
  Logger::infof("RADIO",
                "ranging_profile freq=%.1f MHz bw=%.1f kHz sf=%u cr=4/%u sync=private power=%d dBm",
                static_cast<double>(kRangingFrequencyMhz),
                static_cast<double>(kRangingBandwidthKhz),
                static_cast<unsigned>(kRangingSpreadingFactor),
                static_cast<unsigned>(kRangingCodingRate),
                static_cast<int>(kRangingTxPowerDbm));

  if (!waitBusyLow(1000)) {
    gStatus.lastError = RADIOLIB_ERR_SPI_CMD_TIMEOUT;
    publishStatus(HealthState::Fail);
    Logger::failf("RADIO", "BUSY pin stayed HIGH before init");
    vTaskDelete(nullptr);
  }

  const int16_t state = radio.begin(kRangingFrequencyMhz,
                                    kRangingBandwidthKhz,
                                    kRangingSpreadingFactor,
                                    kRangingCodingRate,
                                    kRangingSyncWord,
                                    kRangingTxPowerDbm,
                                    kRangingPreambleLen);
  gStatus.lastError = state;
  if (state != RADIOLIB_ERR_NONE) {
    publishStatus(HealthState::Fail);
    Logger::failf("RADIO", "SX128x ranging init failed, code=%d", state);
    vTaskDelete(nullptr);
  }

  gStatus.initialized = true;
  publishStatus(HealthState::Warn);
  Logger::okf("RADIO", "SX128x ranging initialized");
}

}  // namespace

extern "C" void RadioHealthTask(void *) {
  initRadio();

  NodeConfig config;
  uint8_t lastNodeId = NAV_INVALID_NODE_ID;
  uint8_t nextPeerId = NAV_INVALID_NODE_ID;
  uint32_t nextMasterAtMs = 0u;
  uint32_t lastConfigLogMs = 0u;

  for (;;) {
    if (!ControlChannel::getConfig(&config)) {
      delayMs(kConfigPollMs);
      continue;
    }
    if (!isValidNodeId(config.nodeId)) {
      if ((nowMs() - lastConfigLogMs) > 2000u) {
        lastConfigLogMs = nowMs();
        Logger::warnf("RANGE", "invalid node_id=%u; set node id 0..3 in control app", static_cast<unsigned>(config.nodeId));
      }
      delayMs(kConfigPollMs);
      continue;
    }
    if (config.nodeId != lastNodeId) {
      lastNodeId = config.nodeId;
      nextPeerId = nextPeerAfter(config.nodeId, config.nodeId);
      nextMasterAtMs = initialMasterAt(config.nodeId);
      Logger::infof("RANGE",
                    "distance-only role node=%u role=single-hop-discovery next_peer=%u",
                    static_cast<unsigned>(config.nodeId),
                    static_cast<unsigned>(nextPeerId));
    }

    const uint32_t now = nowMs();
    if (timeReached(now, nextMasterAtMs)) {
      if (nextPeerId != NAV_INVALID_NODE_ID) {
        runMasterExchange(config.nodeId, nextPeerId);
        nextPeerId = nextPeerAfter(config.nodeId, nextPeerId);
      }
      nextMasterAtMs = nowMs() + kMasterScanIntervalMs;
      delayMs(kMasterPeerGapMs);
    } else {
      const uint32_t listenWindowMs = listenWindowUntil(now, nextMasterAtMs);
      if (listenWindowMs < 20u) {
        delayMs(listenWindowMs == 0u ? 1u : listenWindowMs);
      } else {
        serviceNetworkListen(config.nodeId, listenWindowMs);
      }
    }
  }
}
