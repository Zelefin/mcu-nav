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
#include "nav/nav_tdma.h"
#include "nav/nav_types.h"
#include "nav/nav_telemetry.h"

namespace {
constexpr float kRangingFrequencyMhz = 2445.0f;
constexpr float kRangingBandwidthKhz = 1625.0f;
constexpr uint8_t kRangingSpreadingFactor = 7u;
constexpr uint8_t kRangingCodingRate = 5u;
constexpr uint8_t kRangingSyncWord = RADIOLIB_SX128X_SYNC_WORD_PRIVATE;
static_assert(RADIO_TX_POWER_DBM >= -18 && RADIO_TX_POWER_DBM <= 13,
              "RADIO_TX_POWER_DBM must be in the SX1280 SetTxParams range -18..13 dBm");
constexpr int8_t kRangingTxPowerDbm = RADIO_TX_POWER_DBM;
constexpr uint16_t kRangingPreambleLen = 12u;
constexpr uint32_t kRangingAddressBase = 0x4E415600UL;  // "NAV" + slave id
constexpr uint32_t kMasterTimeoutMs = 250u;
constexpr uint32_t kRangingMasterStartGuardMs = 180u;
constexpr uint32_t kTdmaHeartbeatRxCompensationMs = 100u;
constexpr uint32_t kSlaveListenSliceMs = 120u;
constexpr uint32_t kReportRxWindowMs = 45u;
constexpr uint8_t kReportTxRepeats = 3u;
constexpr uint32_t kReportTxGapMs = 8u;
constexpr uint32_t kBestEffortTxGuardMs = 80u;
constexpr uint8_t kTelemetryBeaconRepeats = 3u;
constexpr uint32_t kTelemetryBeaconRepeatGapMs = 55u;
constexpr uint32_t kDebugEnablePeriodMs = 1000u;
constexpr uint16_t kDebugEnableTtlMs = 15000u;
constexpr uint32_t kNodeQualityReportPeriodMs = 5000u;
constexpr uint32_t kConfigPollMs = 250u;
constexpr uint32_t kRadioInitRetryMs = 5000u;
constexpr uint32_t kRangeSigmaMm = 1000u;
constexpr size_t kRangeReportPayloadMax = 224u;

static_assert(NAV_TDMA_FIXED_SLOT_MS > kMasterTimeoutMs,
              "TDMA slot must be longer than the SX1280 master timeout");
static_assert(NAV_TDMA_FIXED_SLOT_MS >= (kRangingMasterStartGuardMs + kMasterTimeoutMs + 20u),
              "TDMA slot must leave room for the master start guard and timeout");

struct DebugTelemetryState {
  uint32_t remoteDebugActiveUntilMs;
  uint32_t lastBeaconTxMs;
  uint32_t lastDebugEnableTxMs;
  uint32_t lastNodeQualityTxMs;
  uint32_t beaconPacketSeq;
  uint32_t nodeQualityPacketSeq;
  uint16_t txFrameSeq;
  bool remoteDebugWasActive;
};

struct TdmaRuntimeState {
  nav_tdma_t schedule;
  uint8_t localNodeId;
  uint32_t epochLocalMs;
  uint32_t lastAuthorityHeartbeatMs;
  uint32_t lastProcessedAbsoluteSlot;
  uint16_t txFrameSeq;
  bool initialized;
  bool authoritySynced;
  bool waitingLogged;
};

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

uint32_t remainingInWindow(uint32_t startedMs, uint32_t windowMs) {
  const uint32_t elapsedMs = nowMs() - startedMs;
  return elapsedMs >= windowMs ? 0u : windowMs - elapsedMs;
}

uint32_t tdmaAbsoluteSlot(const nav_tdma_t &schedule, const nav_tdma_action_t &action) {
  const size_t slots = nav_tdma_slots_per_frame(&schedule);
  return (action.frame_index * static_cast<uint32_t>(slots)) + action.slot_index;
}

void resetTdmaRuntime(TdmaRuntimeState *tdma, uint8_t nodeId) {
  if (!tdma) {
    return;
  }
  *tdma = {};
  tdma->localNodeId = nodeId;
  nav_tdma_init(&tdma->schedule, nodeId, NAV_TDMA_FIXED_SLOT_MS);
  (void)nav_tdma_set_fixed_field_members(&tdma->schedule);
  tdma->lastProcessedAbsoluteSlot = UINT32_MAX;
  tdma->authoritySynced = nodeId == NAV_TDMA_AUTHORITY_ID;
  tdma->epochLocalMs = 0u;
  tdma->initialized = true;
}

bool tdmaHasAuthorityTiming(const TdmaRuntimeState &tdma, uint32_t now) {
  if (tdma.localNodeId == NAV_TDMA_AUTHORITY_ID) {
    return true;
  }
  return tdma.authoritySynced && !nav_tdma_authority_expired(now, tdma.lastAuthorityHeartbeatMs);
}

uint32_t tdmaAlignedNow(const TdmaRuntimeState &tdma, uint32_t now) {
  return tdma.localNodeId == NAV_TDMA_AUTHORITY_ID ? now : now - tdma.epochLocalMs;
}

uint32_t tdmaLocalSlotEnd(const TdmaRuntimeState &tdma, const nav_tdma_action_t &action) {
  return tdma.localNodeId == NAV_TDMA_AUTHORITY_ID ? action.slot_end_ms : tdma.epochLocalMs + action.slot_end_ms;
}

uint32_t tdmaLocalSlotStart(const TdmaRuntimeState &tdma, const nav_tdma_action_t &action) {
  return tdma.localNodeId == NAV_TDMA_AUTHORITY_ID ? action.slot_start_ms : tdma.epochLocalMs + action.slot_start_ms;
}

const char *tdmaActionName(const nav_tdma_action_t &action) {
  switch (action.type) {
    case NAV_TDMA_TX_BEACON:
      return "beacon";
    case NAV_TDMA_RANGE_PEER:
      return "range";
    case NAV_TDMA_LISTEN:
    default:
      return "listen";
  }
}

const char *tdmaRoleName(uint8_t nodeId, const nav_tdma_action_t &action) {
  if (nodeId == NAV_TDMA_AUTHORITY_ID && action.type == NAV_TDMA_TX_BEACON) {
    return "authority";
  }
  if (action.type == NAV_TDMA_TX_BEACON && action.from_id == nodeId) {
    return "follower";
  }
  if (action.type == NAV_TDMA_RANGE_PEER && action.from_id == nodeId) {
    return "ranging_master";
  }
  if (action.type == NAV_TDMA_LISTEN && action.to_id == nodeId) {
    return "ranging_slave";
  }
  return "listener";
}

const char *tdmaSyncStateName(const TdmaRuntimeState &tdma, uint32_t now) {
  if (tdma.localNodeId == NAV_TDMA_AUTHORITY_ID) {
    return "authority";
  }
  if (!tdma.authoritySynced) {
    return "waiting";
  }
  if (nav_tdma_authority_expired(now, tdma.lastAuthorityHeartbeatMs)) {
    return "expired";
  }
  return "synced";
}

uint32_t authorityAgeMs(const TdmaRuntimeState &tdma, uint32_t now) {
  if (tdma.localNodeId == NAV_TDMA_AUTHORITY_ID || tdma.lastAuthorityHeartbeatMs == 0u) {
    return 0u;
  }
  return now - tdma.lastAuthorityHeartbeatMs;
}

void logTdmaSlot(const char *event,
                 const TdmaRuntimeState &tdma,
                 const nav_tdma_action_t &action,
                 uint32_t localNow,
                 uint32_t remainingMs,
                 const char *reason) {
  Logger::infof("TDMA",
                "event=%s local_ms=%lu node_id=%u frame_index=%lu slot_index=%lu slot_ms=%u remaining_ms=%lu action=%s role=%s from_id=%u to_id=%u peer_id=%u authority_age_ms=%lu sync_state=%s reason=%s",
                event,
                static_cast<unsigned long>(localNow),
                static_cast<unsigned>(tdma.localNodeId),
                static_cast<unsigned long>(action.frame_index),
                static_cast<unsigned long>(action.slot_index),
                static_cast<unsigned>(NAV_TDMA_FIXED_SLOT_MS),
                static_cast<unsigned long>(remainingMs),
                tdmaActionName(action),
                tdmaRoleName(tdma.localNodeId, action),
                static_cast<unsigned>(action.from_id),
                static_cast<unsigned>(action.to_id),
                static_cast<unsigned>(action.peer_id),
                static_cast<unsigned long>(authorityAgeMs(tdma, localNow)),
                tdmaSyncStateName(tdma, localNow),
                reason ? reason : "none");
}

void logTdmaAuthority(const char *event,
                      const TdmaRuntimeState &tdma,
                      uint32_t localNow,
                      const char *reason,
                      const nav_tdma_timing_heartbeat_t *heartbeat) {
  Logger::infof("TDMA",
                "event=%s local_ms=%lu node_id=%u frame_index=%lu slot_index=%lu slot_ms=%u slot_elapsed_ms=%u authority_age_ms=%lu sync_state=%s reason=%s",
                event,
                static_cast<unsigned long>(localNow),
                static_cast<unsigned>(tdma.localNodeId),
                static_cast<unsigned long>(heartbeat ? heartbeat->frame_index : 0u),
                static_cast<unsigned long>(heartbeat ? heartbeat->slot_index : 0u),
                static_cast<unsigned>(heartbeat ? heartbeat->slot_ms : 0u),
                static_cast<unsigned>(heartbeat ? heartbeat->slot_elapsed_ms : 0u),
                static_cast<unsigned long>(authorityAgeMs(tdma, localNow)),
                tdmaSyncStateName(tdma, localNow),
                reason ? reason : "none");
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

void configureOutputPin(int pin, int level) {
  if (pin < 0 || pin >= GPIO_NUM_MAX) {
    return;
  }
  gpio_config_t config = {};
  config.pin_bit_mask = (1ULL << pin);
  config.mode = GPIO_MODE_OUTPUT;
  config.pull_up_en = GPIO_PULLUP_DISABLE;
  config.pull_down_en = GPIO_PULLDOWN_DISABLE;
  config.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&config);
  gpio_set_level(static_cast<gpio_num_t>(pin), level);
}

void delayMs(uint32_t ms) {
  TickType_t ticks = pdMS_TO_TICKS(ms);
  if (ticks == 0) {
    ticks = 1;
  }
  vTaskDelay(ticks);
}

void pulseRadioReset(uint32_t attempt) {
  if (PIN_LORA_RST < 0 || PIN_LORA_RST >= GPIO_NUM_MAX) {
    return;
  }
  Logger::infof("RADIO",
                "Pulsing SX128x reset before init attempt=%lu rst_pin=%d",
                static_cast<unsigned long>(attempt),
                PIN_LORA_RST);
  configureOutputPin(PIN_LORA_RST, 1);
  delayMs(2);
  gpio_set_level(static_cast<gpio_num_t>(PIN_LORA_RST), 0);
  delayMs(10);
  gpio_set_level(static_cast<gpio_num_t>(PIN_LORA_RST), 1);
  delayMs(20);
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

nav_range_fail_reason_t parseRangeFailReason(const char *name) {
  if (!name) {
    return NAV_RANGE_FAIL_UNKNOWN;
  }
  if (strcmp(name, "TIMEOUT") == 0) return NAV_RANGE_FAIL_TIMEOUT;
  if (strcmp(name, "NO_RESPONSE") == 0) return NAV_RANGE_FAIL_NO_RESPONSE;
  if (strcmp(name, "RADIO_BUSY") == 0) return NAV_RANGE_FAIL_RADIO_BUSY;
  if (strcmp(name, "BAD_FRAME") == 0) return NAV_RANGE_FAIL_BAD_FRAME;
  if (strcmp(name, "RANGING_ENGINE_ERROR") == 0) return NAV_RANGE_FAIL_RANGING_ENGINE_ERROR;
  if (strcmp(name, "ABORTED") == 0) return NAV_RANGE_FAIL_ABORTED;
  if (strcmp(name, "NONE") == 0) return NAV_RANGE_FAIL_NONE;
  return NAV_RANGE_FAIL_UNKNOWN;
}

bool parseLegacyRangeReport(const char *text,
                            uint32_t timestampMs,
                            int16_t rssiDbm,
                            int16_t snrDb,
                            nav_serial_range_record_t *out) {
  if (!text || !out) {
    return false;
  }

  unsigned fromId = 0u;
  unsigned toId = 0u;
  unsigned requestId = 0u;
  unsigned long rangeMm = 0u;
  if (sscanf(text,
             "range_result ok=true from=%u to=%u request_id=%u range_mm=%lu",
             &fromId,
             &toId,
             &requestId,
             &rangeMm) == 4) {
    *out = {};
    out->timestamp_ms = timestampMs;
    out->from_id = static_cast<uint8_t>(fromId);
    out->to_id = static_cast<uint8_t>(toId);
    out->request_id = static_cast<uint16_t>(requestId);
    out->ok = true;
    out->range_mm = static_cast<uint32_t>(rangeMm);
    out->range_sigma_mm = kRangeSigmaMm;
    out->rssi_dbm = rssiDbm;
    out->snr_db = snrDb;
    out->source = "air_report";
    return fromId < NAV_MAX_NODES && toId < NAV_MAX_NODES && requestId <= 65535u && rangeMm <= UINT32_MAX;
  }

  char reason[32] = {};
  if (sscanf(text,
             "range_result ok=false from=%u to=%u request_id=%u range_fail_reason=%31s",
             &fromId,
             &toId,
             &requestId,
             reason) == 4) {
    *out = {};
    out->timestamp_ms = timestampMs;
    out->from_id = static_cast<uint8_t>(fromId);
    out->to_id = static_cast<uint8_t>(toId);
    out->request_id = static_cast<uint16_t>(requestId);
    out->ok = false;
    out->rssi_dbm = rssiDbm;
    out->snr_db = snrDb;
    out->range_fail_reason = parseRangeFailReason(reason);
    out->source = "air_report";
    return fromId < NAV_MAX_NODES && toId < NAV_MAX_NODES && requestId <= 65535u;
  }

  return false;
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

void ingestLocalEndpointAirReport(const nav_serial_range_record_t &record) {
  NodeConfig config = {};
  if (!ControlChannel::getConfig(&config)) {
    return;
  }
  if (record.from_id != config.nodeId && record.to_id != config.nodeId) {
    return;
  }

  const uint8_t peerId = record.from_id == config.nodeId ? record.to_id : record.from_id;
  if (peerId == config.nodeId || !isValidNodeId(peerId)) {
    return;
  }

  nav_event_t event = {};
  event.timestamp_ms = record.timestamp_ms;
  if (record.ok) {
    event.type = NAV_EVT_RANGE_RESULT;
    event.data.range_result.peer_id = peerId;
    event.data.range_result.request_id = record.request_id;
    event.data.range_result.timestamp_ms = record.timestamp_ms;
    event.data.range_result.range_mm = record.range_mm;
    event.data.range_result.range_sigma_mm = record.range_sigma_mm;
    event.data.range_result.rssi_dbm = record.rssi_dbm;
    event.data.range_result.snr_db = record.snr_db;
    event.data.range_result.valid = true;
  } else {
    event.type = NAV_EVT_RANGE_FAIL;
    event.data.range_failure.peer_id = peerId;
    event.data.range_failure.request_id = record.request_id;
    event.data.range_failure.timestamp_ms = record.timestamp_ms;
    event.data.range_failure.reason =
        record.range_fail_reason == NAV_RANGE_FAIL_NONE ? NAV_RANGE_FAIL_UNKNOWN : record.range_fail_reason;
  }
  (void)ControlChannel::handleEvent(&event);
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

bool transmitTypedReport(const uint8_t *payload, size_t payloadLen, const char *kind) {
  if (!payload || payloadLen == 0u) {
    return false;
  }
  if (!waitBusyLow(50)) {
    Logger::warnf("RADIO", "%s tx=false error=%d note=\"BUSY stayed high\"", kind, RADIOLIB_ERR_SPI_CMD_TIMEOUT);
    return false;
  }
  const int16_t state = radio.transmit(payload, payloadLen);
  if (state != RADIOLIB_ERR_NONE) {
    Logger::warnf("RADIO", "%s tx=false error=%d", kind, state);
    return false;
  }
  return true;
}

uint8_t transmitTypedReportRepeated(const uint8_t *payload, size_t payloadLen, const char *kind, uint8_t repeats, uint32_t gapMs) {
  uint8_t sent = 0u;
  for (uint8_t attempt = 0u; attempt < repeats; ++attempt) {
    if (transmitTypedReport(payload, payloadLen, kind)) {
      ++sent;
    }
    if ((attempt + 1u) < repeats) {
      delayMs(gapMs);
    }
  }
  return sent;
}

bool debugTelemetryRemoteActive(const DebugTelemetryState &state, uint32_t now) {
  return state.remoteDebugActiveUntilMs != 0u && !timeReached(now, state.remoteDebugActiveUntilMs);
}

bool periodDue(uint32_t now, uint32_t lastTxMs, uint32_t periodMs) {
  return lastTxMs == 0u || (now - lastTxMs) >= periodMs;
}

void handleDebugEnableFrame(const nav_radio_frame_t &frame, uint8_t nodeId, DebugTelemetryState *debugState) {
  if (!debugState) {
    return;
  }
  nav_debug_enable_t debug = {};
  const nav_status_t status = nav_telemetry_decode_debug_enable(&frame, &debug);
  if (status != NAV_STATUS_OK) {
    Logger::warnf("RADIO", "debug_enable rx=false status=%d", static_cast<int>(status));
    return;
  }

  const uint32_t now = nowMs();
  const bool wasActive = debugTelemetryRemoteActive(*debugState, now);
  debugState->remoteDebugActiveUntilMs = now + static_cast<uint32_t>(debug.ttl_ms);
  debugState->lastNodeQualityTxMs = 0u;
  debugState->remoteDebugWasActive = true;
  if (!wasActive) {
    Logger::infof("RADIO",
                  "debug_telemetry active=true origin=%u heard_by=%u ttl_ms=%u",
                  static_cast<unsigned>(debug.origin_node_id),
                  static_cast<unsigned>(nodeId),
                  static_cast<unsigned>(debug.ttl_ms));
  }
}

void handleNodeQualityFrame(const nav_radio_frame_t &frame, uint8_t nodeId) {
  nav_node_quality_report_t report = {};
  const nav_status_t status = nav_telemetry_decode_node_quality_report(&frame, &report);
  if (status != NAV_STATUS_OK) {
    Logger::warnf("RADIO", "node_quality rx=false status=%d", static_cast<int>(status));
    return;
  }
  if (!ControlChannel::handleNodeQualityReport(&report, nowMs())) {
    Logger::warnf("RADIO",
                  "node_quality rx=false node=%u heard_by=%u note=\"buffer rejected\"",
                  static_cast<unsigned>(report.node_id),
                  static_cast<unsigned>(nodeId));
    return;
  }
  Logger::debugf("RADIO",
                 "node_quality rx=true node=%u heard_by=%u packet_seq=%lu",
                 static_cast<unsigned>(report.node_id),
                 static_cast<unsigned>(nodeId),
                 static_cast<unsigned long>(report.packet_seq));
}

void handleHeartbeatFrame(const nav_radio_frame_t &frame, uint8_t nodeId, TdmaRuntimeState *tdmaState) {
  if (!tdmaState || nodeId == NAV_TDMA_AUTHORITY_ID) {
    return;
  }
  nav_radio_heartbeat_payload_t heartbeat = {};
  const nav_status_t status = nav_telemetry_decode_heartbeat(&frame, &heartbeat);
  if (status != NAV_STATUS_OK) {
    Logger::warnf("TDMA",
                  "event=authority_sync node_id=%u ok=false reason=bad_frame status=%d",
                  static_cast<unsigned>(nodeId),
                  static_cast<int>(status));
    return;
  }

  nav_tdma_timing_heartbeat_t timing = {};
  timing.authority_id = heartbeat.node_id_u8;
  timing.frame_index = heartbeat.tdma_frame_index_u32;
  timing.slot_index = heartbeat.tdma_slot_index_u8;
  timing.slot_ms = heartbeat.tdma_slot_ms_u16;
  timing.slot_elapsed_ms = heartbeat.tdma_slot_elapsed_ms_u16;

  const uint32_t now = nowMs();
  const nav_status_t valid = nav_tdma_validate_timing_heartbeat(&tdmaState->schedule, &timing);
  if (valid != NAV_STATUS_OK) {
    logTdmaAuthority("authority_mismatch", *tdmaState, now, "tdma_config_mismatch", &timing);
    return;
  }

  const uint32_t slotsPerFrame = static_cast<uint32_t>(nav_tdma_slots_per_frame(&tdmaState->schedule));
  const uint32_t absoluteSlot = (timing.frame_index * slotsPerFrame) + timing.slot_index;
  const uint32_t authorityTimeMs =
      (absoluteSlot * static_cast<uint32_t>(timing.slot_ms)) +
      static_cast<uint32_t>(timing.slot_elapsed_ms) +
      kTdmaHeartbeatRxCompensationMs;
  tdmaState->epochLocalMs = now - authorityTimeMs;
  tdmaState->lastAuthorityHeartbeatMs = now;
  tdmaState->authoritySynced = true;
  tdmaState->waitingLogged = false;
  logTdmaAuthority("authority_sync", *tdmaState, now, "ok", &timing);
}

void handleBeaconFrame(const uint8_t *payload, size_t payloadLen, uint8_t nodeId, int16_t rssiDbm, int16_t snrDb) {
  nav_event_t event = {};
  const nav_status_t status = nav_telemetry_decode_event(payload, payloadLen, nowMs(), rssiDbm, snrDb, &event);
  if (status != NAV_STATUS_OK || event.type != NAV_EVT_PEER_TELEMETRY_RX) {
    Logger::warnf("RADIO", "beacon rx=false status=%d", static_cast<int>(status));
    return;
  }
  const uint8_t peerId = event.data.peer_beacon_rx.telemetry.node_id;
  if (peerId == nodeId) {
    return;
  }
  if (!ControlChannel::handleEvent(&event)) {
    Logger::warnf("RADIO", "beacon rx=false peer=%u note=\"core rejected\"", static_cast<unsigned>(peerId));
    return;
  }
  Logger::debugf("RADIO",
                 "beacon rx=true peer=%u heard_by=%u packet_seq=%lu gnss=%u",
                 static_cast<unsigned>(peerId),
                 static_cast<unsigned>(nodeId),
                 static_cast<unsigned long>(event.data.peer_beacon_rx.telemetry.packet_seq),
                 event.data.peer_beacon_rx.telemetry.gnss_valid ? 1u : 0u);
}

void dispatchTypedReport(const uint8_t *payload,
                         size_t payloadLen,
                         uint8_t nodeId,
                         int16_t rssiDbm,
                         int16_t snrDb,
                         DebugTelemetryState *debugState,
                         TdmaRuntimeState *tdmaState) {
  nav_radio_frame_t frame;
  const nav_status_t status = nav_radio_decode_frame(payload, payloadLen, &frame);
  if (status != NAV_STATUS_OK) {
    return;
  }

  switch (frame.type) {
    case NAV_RADIO_MSG_BEACON_RX:
      handleBeaconFrame(payload, payloadLen, nodeId, rssiDbm, snrDb);
      break;
    case NAV_RADIO_MSG_HEARTBEAT:
      handleHeartbeatFrame(frame, nodeId, tdmaState);
      break;
    case NAV_RADIO_MSG_DEBUG_ENABLE:
      handleDebugEnableFrame(frame, nodeId, debugState);
      break;
    case NAV_RADIO_MSG_NODE_QUALITY_REPORT:
      handleNodeQualityFrame(frame, nodeId);
      break;
    default:
      Logger::debugf("RADIO",
                     "best_effort rx=true type=%s note=\"ignored\"",
                     nav_radio_message_type_to_string(frame.type));
      break;
  }
}

void serviceReportRx(uint8_t nodeId, uint32_t windowMs, DebugTelemetryState *debugState, TdmaRuntimeState *tdmaState) {
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
  uint8_t payload[kRangeReportPayloadMax] = {};
  const size_t readLen = packetLen < kRangeReportPayloadMax ? packetLen : kRangeReportPayloadMax;
  const int16_t state = radio.readData(payload, readLen);

  if (state != RADIOLIB_ERR_NONE) {
    return;
  }

  if (readLen >= 5u && memcmp(payload, "NRR1 ", 5u) == 0) {
    char text[kRangeReportPayloadMax + 1u] = {};
    memcpy(text, payload, readLen);
    text[readLen] = '\0';
    sanitizeReportText(text);

    const float reportRssi = radio.getRSSI();
    const float reportSnr = radio.getSNR();
    Logger::infof("RANGE",
                  "%s source=air_report heard_by=%u report_rssi_dbm=%.1f report_snr_db=%.1f",
                  text + 5u,
                  static_cast<unsigned>(nodeId),
                  static_cast<double>(reportRssi),
                  static_cast<double>(reportSnr));
    nav_serial_range_record_t rangeRecord = {};
    if (parseLegacyRangeReport(text + 5u,
                               nowMs(),
                               static_cast<int16_t>(std::lround(reportRssi)),
                               static_cast<int16_t>(std::lround(reportSnr)),
                               &rangeRecord)) {
      (void)ControlChannel::emitRangeRecord(&rangeRecord);
      ingestLocalEndpointAirReport(rangeRecord);
    }
    return;
  }

  const int16_t rssiDbm = static_cast<int16_t>(std::lround(radio.getRSSI()));
  const int16_t snrDb = static_cast<int16_t>(std::lround(radio.getSNR()));
  dispatchTypedReport(payload, readLen, nodeId, rssiDbm, snrDb, debugState, tdmaState);
}

bool broadcastTelemetryBeacon(DebugTelemetryState *debugState, uint32_t now) {
  if (!debugState) {
    return false;
  }
  debugState->lastBeaconTxMs = now;

  const uint32_t packetSeq = debugState->beaconPacketSeq + 1u;
  nav_peer_telemetry_t telemetry = {};
  if (!ControlChannel::getLocalTelemetry(packetSeq, &telemetry)) {
    return false;
  }
  debugState->beaconPacketSeq = packetSeq;

  uint8_t frame[NAV_RADIO_MAX_FRAME_BYTES];
  size_t frameLen = 0u;
  const nav_status_t status =
      nav_telemetry_encode_beacon(&telemetry, ++debugState->txFrameSeq, frame, sizeof(frame), &frameLen);
  if (status != NAV_STATUS_OK) {
    Logger::warnf("RADIO", "beacon tx=false status=%d", static_cast<int>(status));
    return false;
  }
  const uint8_t sent = transmitTypedReportRepeated(frame,
                                                   frameLen,
                                                   "beacon",
                                                   kTelemetryBeaconRepeats,
                                                   kTelemetryBeaconRepeatGapMs);
  Logger::infof("RADIO",
                "beacon tx=%s packet_seq=%lu repeats=%u sent=%u",
                sent > 0u ? "true" : "false",
                static_cast<unsigned long>(packetSeq),
                static_cast<unsigned>(kTelemetryBeaconRepeats),
                static_cast<unsigned>(sent));
  return sent > 0u;
}

bool broadcastTimingHeartbeat(const nav_tdma_action_t &action, TdmaRuntimeState *tdma) {
  if (!tdma || tdma->localNodeId != NAV_TDMA_AUTHORITY_ID) {
    return false;
  }
  nav_radio_heartbeat_payload_t heartbeat = {};
  heartbeat.node_id_u8 = tdma->localNodeId;
  heartbeat.uptime_ms_u32 = nowMs();
  heartbeat.status_flags_u32 = 0u;
  heartbeat.tdma_frame_index_u32 = action.frame_index;
  heartbeat.tdma_slot_index_u8 = static_cast<uint8_t>(action.slot_index);
  heartbeat.tdma_slot_ms_u16 = static_cast<uint16_t>(NAV_TDMA_FIXED_SLOT_MS);
  const uint32_t elapsedMs = heartbeat.uptime_ms_u32 - tdmaLocalSlotStart(*tdma, action);
  heartbeat.tdma_slot_elapsed_ms_u16 =
      static_cast<uint16_t>(elapsedMs < NAV_TDMA_FIXED_SLOT_MS ? elapsedMs : (NAV_TDMA_FIXED_SLOT_MS - 1u));

  uint8_t frame[NAV_RADIO_MAX_FRAME_BYTES];
  size_t frameLen = 0u;
  const nav_status_t status =
      nav_telemetry_encode_heartbeat(&heartbeat, ++tdma->txFrameSeq, frame, sizeof(frame), &frameLen);
  if (status != NAV_STATUS_OK) {
    Logger::warnf("TDMA", "event=authority_heartbeat_tx node_id=%u ok=false status=%d", static_cast<unsigned>(tdma->localNodeId), static_cast<int>(status));
    return false;
  }
  const bool ok = transmitTypedReport(frame, frameLen, "heartbeat");
  Logger::infof("TDMA",
                "event=authority_heartbeat_tx node_id=%u ok=%u frame_index=%lu slot_index=%lu slot_ms=%u slot_elapsed_ms=%u",
                static_cast<unsigned>(tdma->localNodeId),
                ok ? 1u : 0u,
                static_cast<unsigned long>(action.frame_index),
                static_cast<unsigned long>(action.slot_index),
                static_cast<unsigned>(NAV_TDMA_FIXED_SLOT_MS),
                static_cast<unsigned>(heartbeat.tdma_slot_elapsed_ms_u16));
  return ok;
}

void broadcastDebugEnable(uint8_t nodeId, DebugTelemetryState *debugState) {
  if (!debugState) {
    return;
  }
  nav_debug_enable_t debug = {};
  debug.origin_node_id = nodeId;
  debug.ttl_ms = kDebugEnableTtlMs;

  uint8_t frame[NAV_RADIO_MAX_FRAME_BYTES];
  size_t frameLen = 0u;
  const nav_status_t status =
      nav_telemetry_encode_debug_enable(&debug, ++debugState->txFrameSeq, frame, sizeof(frame), &frameLen);
  if (status != NAV_STATUS_OK) {
    Logger::warnf("RADIO", "debug_enable tx=false status=%d", static_cast<int>(status));
    return;
  }
  (void)transmitTypedReport(frame, frameLen, "debug_enable");
}

void broadcastNodeQualityReport(DebugTelemetryState *debugState) {
  if (!debugState) {
    return;
  }
  nav_node_quality_report_t report = {};
  const uint32_t packetSeq = ++debugState->nodeQualityPacketSeq;
  if (!ControlChannel::getLocalNodeQualityReport(packetSeq, &report)) {
    return;
  }

  uint8_t frame[NAV_RADIO_MAX_FRAME_BYTES];
  size_t frameLen = 0u;
  const nav_status_t status =
      nav_telemetry_encode_node_quality_report(&report, ++debugState->txFrameSeq, frame, sizeof(frame), &frameLen);
  if (status != NAV_STATUS_OK) {
    Logger::warnf("RADIO", "node_quality tx=false status=%d", static_cast<int>(status));
    return;
  }
  (void)transmitTypedReport(frame, frameLen, "node_quality");
}

void serviceDebugTelemetryTx(uint8_t nodeId, uint32_t remainingMs, DebugTelemetryState *debugState) {
  if (!debugState || remainingMs < (kBestEffortTxGuardMs + kSlaveListenSliceMs)) {
    return;
  }

  const uint32_t now = nowMs();
  if (ControlChannel::isDebugEnabled() && periodDue(now, debugState->lastDebugEnableTxMs, kDebugEnablePeriodMs)) {
    debugState->lastDebugEnableTxMs = now;
    broadcastDebugEnable(nodeId, debugState);
    return;
  }

  if (!debugTelemetryRemoteActive(*debugState, now)) {
    if (debugState->remoteDebugWasActive) {
      debugState->remoteDebugWasActive = false;
      Logger::infof("RADIO", "debug_telemetry active=false note=\"ttl expired\"");
    }
    return;
  }

  debugState->remoteDebugWasActive = true;
  if (periodDue(now, debugState->lastNodeQualityTxMs, kNodeQualityReportPeriodMs)) {
    debugState->lastNodeQualityTxMs = now;
    broadcastNodeQualityReport(debugState);
  }
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
                      const char *note,
                      bool broadcastAirReport) {
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
  if (broadcastAirReport) {
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
}

void runMasterExchange(uint8_t masterId, uint8_t peerId, bool broadcastAirReport) {
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
                     "BUSY stayed high before ranging",
                     broadcastAirReport);
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
                     "startRanging master failed",
                     broadcastAirReport);
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
                     "ranging timeout",
                     broadcastAirReport);
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
                     "finishRanging master failed",
                     broadcastAirReport);
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
    if (broadcastAirReport) {
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
    }
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
  if (broadcastAirReport) {
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
  }
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

void servicePassiveListen(uint8_t nodeId,
                          uint32_t listenWindowMs,
                          DebugTelemetryState *debugState,
                          TdmaRuntimeState *tdmaState,
                          bool allowDebugTx) {
  const uint32_t startedMs = nowMs();
  while ((nowMs() - startedMs) < listenWindowMs) {
    uint32_t remainingMs = remainingInWindow(startedMs, listenWindowMs);
    if (allowDebugTx) {
      serviceDebugTelemetryTx(nodeId, remainingMs, debugState);
    }
    remainingMs = remainingInWindow(startedMs, listenWindowMs);
    if (remainingMs >= (kReportRxWindowMs + 20u)) {
      serviceReportRx(nodeId, kReportRxWindowMs, debugState, tdmaState);
      remainingMs = remainingInWindow(startedMs, listenWindowMs);
    }
    if (remainingMs < 20u) {
      break;
    }
    delayMs(remainingMs < 20u ? remainingMs : 20u);
  }
}

void listenUntilLocalTime(uint8_t nodeId,
                          uint32_t localDeadlineMs,
                          DebugTelemetryState *debugState,
                          TdmaRuntimeState *tdmaState,
                          bool allowDebugTx) {
  const uint32_t now = nowMs();
  if (!timeReached(now, localDeadlineMs)) {
    servicePassiveListen(nodeId, localDeadlineMs - now, debugState, tdmaState, allowDebugTx);
  }
}

void delayUntilLocalTime(uint32_t localDeadlineMs) {
  while (!timeReached(nowMs(), localDeadlineMs)) {
    const uint32_t remainingMs = localDeadlineMs - nowMs();
    delayMs(remainingMs < 10u ? remainingMs : 10u);
  }
}

void completeSlotListening(uint8_t nodeId,
                           const TdmaRuntimeState &tdma,
                           const nav_tdma_action_t &action,
                           DebugTelemetryState *debugState,
                           TdmaRuntimeState *tdmaState,
                           bool allowDebugTx) {
  listenUntilLocalTime(nodeId, tdmaLocalSlotEnd(tdma, action), debugState, tdmaState, allowDebugTx);
}

void serviceTdmaSlot(uint8_t nodeId,
                     TdmaRuntimeState *tdmaState,
                     DebugTelemetryState *debugState) {
  if (!tdmaState || !debugState) {
    return;
  }

  const uint32_t now = nowMs();
  if (!tdmaHasAuthorityTiming(*tdmaState, now)) {
    if (!tdmaState->waitingLogged) {
      logTdmaAuthority("authority_missing", *tdmaState, now, "missing_or_expired", nullptr);
      tdmaState->waitingLogged = true;
    }
    servicePassiveListen(nodeId, NAV_TDMA_FIXED_SLOT_MS, debugState, tdmaState, false);
    return;
  }

  const uint32_t alignedNow = tdmaAlignedNow(*tdmaState, now);
  nav_tdma_action_t action = {};
  if (nav_tdma_action_at(&tdmaState->schedule, alignedNow, &action) != NAV_STATUS_OK) {
    delayMs(20);
    return;
  }

  const uint32_t absoluteSlot = tdmaAbsoluteSlot(tdmaState->schedule, action);
  if (tdmaState->lastProcessedAbsoluteSlot == absoluteSlot) {
    listenUntilLocalTime(nodeId, tdmaLocalSlotEnd(*tdmaState, action), debugState, tdmaState, false);
    return;
  }
  tdmaState->lastProcessedAbsoluteSlot = absoluteSlot;

  logTdmaSlot("slot_start", *tdmaState, action, now, action.remaining_ms, "begin");
  if (action.slot_index == 0u) {
    logTdmaSlot("frame_start", *tdmaState, action, now, action.remaining_ms, "begin");
  }
  const bool canStart = nav_tdma_action_can_start(&action);
  if (!canStart && action.type != NAV_TDMA_LISTEN) {
    logTdmaSlot("slot_missed", *tdmaState, action, now, action.remaining_ms, "late");
    completeSlotListening(nodeId, *tdmaState, action, debugState, tdmaState, false);
    logTdmaSlot("slot_complete", *tdmaState, action, nowMs(), 0u, "missed");
    return;
  }

  switch (action.type) {
    case NAV_TDMA_TX_BEACON: {
      if (nodeId == NAV_TDMA_AUTHORITY_ID) {
        (void)broadcastTimingHeartbeat(action, tdmaState);
      }
      const bool beaconOk = broadcastTelemetryBeacon(debugState, nowMs());
      logTdmaSlot("slot_decision", *tdmaState, action, nowMs(), action.remaining_ms, beaconOk ? "beacon_tx" : "beacon_tx_failed");
      completeSlotListening(nodeId, *tdmaState, action, debugState, tdmaState, true);
      break;
    }
    case NAV_TDMA_RANGE_PEER:
      logTdmaSlot("slot_decision", *tdmaState, action, nowMs(), action.remaining_ms, "range_master");
      delayUntilLocalTime(tdmaLocalSlotStart(*tdmaState, action) + kRangingMasterStartGuardMs);
      runMasterExchange(nodeId, action.peer_id, false);
      completeSlotListening(nodeId, *tdmaState, action, debugState, tdmaState, false);
      break;
    case NAV_TDMA_LISTEN:
    default:
      if (action.to_id == nodeId) {
        logTdmaSlot("slot_decision", *tdmaState, action, nowMs(), action.remaining_ms, "range_slave");
        serviceSlave(nodeId, action.remaining_ms);
        completeSlotListening(nodeId, *tdmaState, action, debugState, tdmaState, false);
      } else {
        logTdmaSlot("slot_decision", *tdmaState, action, nowMs(), action.remaining_ms, "passive_listen");
        completeSlotListening(nodeId, *tdmaState, action, debugState, tdmaState, false);
      }
      break;
  }

  logTdmaSlot("slot_complete", *tdmaState, action, nowMs(), 0u, "done");
}

bool initRadio(uint32_t attempt) {
  configureInputPin(PIN_LORA_BUSY);
  configureInputPin(PIN_LORA_DIO1);
  pulseRadioReset(attempt);

  Logger::infof("RADIO", "Initializing SX128x distance-only ranging attempt=%lu", static_cast<unsigned long>(attempt));
  Logger::infof("RADIO",
                "ranging_profile freq=%.1f MHz bw=%.1f kHz sf=%u cr=4/%u sync=private power=%d dBm",
                static_cast<double>(kRangingFrequencyMhz),
                static_cast<double>(kRangingBandwidthKhz),
                static_cast<unsigned>(kRangingSpreadingFactor),
                static_cast<unsigned>(kRangingCodingRate),
                static_cast<int>(kRangingTxPowerDbm));

  if (!waitBusyLow(1000)) {
    gStatus.initialized = false;
    gStatus.lastError = RADIOLIB_ERR_SPI_CMD_TIMEOUT;
    publishStatus(HealthState::Fail);
    Logger::failf("RADIO",
                  "BUSY pin stayed HIGH before init attempt=%lu retry_in_ms=%lu",
                  static_cast<unsigned long>(attempt),
                  static_cast<unsigned long>(kRadioInitRetryMs));
    return false;
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
    gStatus.initialized = false;
    publishStatus(HealthState::Fail);
    Logger::failf("RADIO",
                  "SX128x ranging init failed, code=%d attempt=%lu retry_in_ms=%lu",
                  state,
                  static_cast<unsigned long>(attempt),
                  static_cast<unsigned long>(kRadioInitRetryMs));
    return false;
  }

  gStatus.initialized = true;
  publishStatus(HealthState::Warn);
  Logger::okf("RADIO", "SX128x ranging initialized attempt=%lu", static_cast<unsigned long>(attempt));
  return true;
}

}  // namespace

extern "C" void RadioHealthTask(void *) {
  uint32_t initAttempt = 1u;
  while (!initRadio(initAttempt)) {
    ++initAttempt;
    delayMs(kRadioInitRetryMs);
  }

  NodeConfig config;
  uint8_t lastNodeId = NAV_INVALID_NODE_ID;
  uint32_t lastConfigLogMs = 0u;
  DebugTelemetryState debugTelemetry = {};
  TdmaRuntimeState tdmaState = {};

  for (;;) {
    if (!ControlChannel::getConfig(&config)) {
      delayMs(kConfigPollMs);
      continue;
    }
    if (!isValidNodeId(config.nodeId)) {
      if ((nowMs() - lastConfigLogMs) > 2000u) {
        lastConfigLogMs = nowMs();
        Logger::warnf("RANGE", "invalid node_id=%u; set node id 0..3 in telemetry UI", static_cast<unsigned>(config.nodeId));
      }
      delayMs(kConfigPollMs);
      continue;
    }
    if (config.nodeId != lastNodeId) {
      lastNodeId = config.nodeId;
      resetTdmaRuntime(&tdmaState, config.nodeId);
      Logger::infof("TDMA",
                    "event=tdma_config node_id=%u authority_id=%u slot_ms=%u frame_slots=%u frame_ms=%lu role=%s",
                    static_cast<unsigned>(config.nodeId),
                    static_cast<unsigned>(NAV_TDMA_AUTHORITY_ID),
                    static_cast<unsigned>(NAV_TDMA_FIXED_SLOT_MS),
                    static_cast<unsigned>(nav_tdma_slots_per_frame(&tdmaState.schedule)),
                    static_cast<unsigned long>(nav_tdma_frame_duration_ms(&tdmaState.schedule)),
                    config.nodeId == NAV_TDMA_AUTHORITY_ID ? "authority" : "follower");
    }

    serviceTdmaSlot(config.nodeId, &tdmaState, &debugTelemetry);
  }
}
