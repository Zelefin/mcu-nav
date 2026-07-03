// SpeedyBee Nano 2.4G (ESP8285 + SX1280) navigation node firmware.
//
// Reuses the portable nav core over the Arduino/ESP8266 framework (ESP-IDF is
// not available on ESP8285). No GPS on this board, so it defaults to
// trilateration and is the ideal target for debugging ranging. Config persists
// in EEPROM; it speaks the same newline-delimited JSON control protocol over
// USB-serial as the ESP32 nodes, so the same telemetry UI drives it.
//
// The default firmware is radio-only: mock peers are compile-gated off so the
// board participates as a real SX1280 node for distance and debug-telemetry
// bring-up.
#include <Arduino.h>
#include <EEPROM.h>
#include <math.h>
#include <string.h>

#include "SX1280.h"
#include "board_pins.h"

extern "C" {
#include "nav/nav_core.h"
#include "nav/nav_mock.h"
#include "nav/nav_quality.h"
#include "nav/nav_radio_protocol.h"
#include "nav/nav_serial_json.h"
#include "nav/nav_state_machine.h"
#include "nav/nav_telemetry.h"
}

namespace {

constexpr uint32_t kBaud = 115200;
constexpr uint32_t kSnapshotPeriodMs = 500;
constexpr uint32_t kHardwareTelemetryTtlMs = 3000;
constexpr uint32_t kHardwareRangeTtlMs = 15000;
constexpr uint32_t kHardwareLocalAltitudeTtlMs = 2000;
constexpr size_t kRecordBufMax = 1536;
constexpr size_t kRangeReportPayloadMax = 224;
constexpr uint8_t kDefaultNodeId = 0;
constexpr int32_t kDefaultAltitudeMm = 183500;
constexpr uint32_t kConfigMagic = 0x5342324eUL;  // "SB2N"
constexpr uint32_t kRadioFrequencyHz = 2445000000UL;
constexpr uint32_t kRangingAddressBase = 0x4E415600UL;  // "NAV" + slave id
constexpr uint16_t kRangingCalibration = 13528;
constexpr uint16_t kMasterExchangeTimeoutMs = 150;
constexpr uint32_t kMasterHostTimeoutMs = 350;
constexpr uint32_t kMasterPeerGapMs = 120;
constexpr uint32_t kSlaveListenWindowMs = 250;
constexpr uint32_t kSlaveListenSliceMs = 120;
constexpr uint32_t kPacketRxWindowMs = 45;
constexpr uint32_t kPacketTxTimeoutMs = 120;
constexpr uint8_t kRangeReportTxRepeats = 3;
constexpr uint32_t kRangeReportTxGapMs = 8;
constexpr uint32_t kBestEffortTxGuardMs = 80;
constexpr uint32_t kDebugEnablePeriodMs = 1000;
constexpr uint16_t kDebugEnableTtlMs = 3000;
constexpr uint32_t kNodeQualityReportPeriodMs = 1000;
constexpr uint32_t kRemoteDebugFallbackTtlMs = 3000;
constexpr uint32_t kMasterScanIntervalMs = 3200;
constexpr uint32_t kInitialMasterDelayMs = 1000;
constexpr uint32_t kMasterTurnSpacingMs = 700;
constexpr uint32_t kRangeSigmaMm = 1000;
constexpr float kShortRangeRawBiasM = 6.2f;
constexpr float kShortRangeCorrectionLimitM = 18.5f;

#ifndef NAV_SPEEDYBEE_ENABLE_MOCK_SOURCE
#define NAV_SPEEDYBEE_ENABLE_MOCK_SOURCE 0
#endif

constexpr bool kMockSourceAvailable = NAV_SPEEDYBEE_ENABLE_MOCK_SOURCE != 0;

struct PersistConfig {
  uint32_t magic;
  uint8_t nodeId;
  char name[NAV_NODE_NAME_MAX];
  uint8_t gpsEnabled;
  uint8_t mockEnabled;
  int32_t altitudeMm;
};

struct BufferedNodeQuality {
  bool present;
  nav_node_quality_report_t report;
  uint32_t receivedMs;
};

struct RangeCorrection {
  float correctedM;
  const char *mode;
};

PersistConfig gConfig;
nav_system_t gNav;
nav_mock_t gMock;
SX1280 gRadio(PIN_RADIO_NSS, PIN_RADIO_RST, PIN_RADIO_BUSY);
char gLine[256];
size_t gLineLen = 0;
uint32_t gLastSnapshotMs = 0;
bool gDebugEnabled = false;
uint32_t gLocalQualityPacketSeq = 0;
bool gRadioReady = false;
uint16_t gRadioFrameSeq = 0;
uint32_t gRemoteDebugActiveUntilMs = 0;
bool gRemoteDebugWasActive = false;
uint32_t gLastDebugEnableTxMs = 0;
uint32_t gLastNodeQualityTxMs = 0;
uint32_t gRadioQualityPacketSeq = 0;
uint32_t gLastRadioNotReadyLogMs = 0;
uint32_t gLastNodeQualityTxLogMs = 0;
uint16_t gRangeRequestId = 0;
uint8_t gRadioNodeId = NAV_INVALID_NODE_ID;
uint8_t gNextPeerId = NAV_INVALID_NODE_ID;
uint32_t gNextMasterAtMs = 0;
BufferedNodeQuality gNodeQuality[NAV_MAX_NODES];

enum PaMode {
  PA_OFF,
  PA_RX,
  PA_TX,
};

void saveConfig() {
  EEPROM.put(0, gConfig);
  EEPROM.commit();
}

void setDefaultConfig() {
  gConfig.magic = kConfigMagic;
  gConfig.nodeId = kDefaultNodeId;
  snprintf(gConfig.name, sizeof(gConfig.name), "speedybee-%u", gConfig.nodeId);
  gConfig.gpsEnabled = 0;  // no GPS hardware -> trilaterate
  gConfig.mockEnabled = 0;
  gConfig.altitudeMm = kDefaultAltitudeMm;
}

void loadConfig() {
  EEPROM.get(0, gConfig);
  if (gConfig.magic != kConfigMagic) {
    setDefaultConfig();
    saveConfig();
    return;
  }

  bool changed = false;
  gConfig.name[sizeof(gConfig.name) - 1] = '\0';
  if (gConfig.nodeId >= NAV_MAX_NODES) {
    gConfig.nodeId = kDefaultNodeId;
    snprintf(gConfig.name, sizeof(gConfig.name), "speedybee-%u", gConfig.nodeId);
    changed = true;
  }
  if (gConfig.gpsEnabled != 0) {
    gConfig.gpsEnabled = 0;
    changed = true;
  }
  if (!kMockSourceAvailable && gConfig.mockEnabled != 0) {
    gConfig.mockEnabled = 0;
    changed = true;
  } else if (gConfig.mockEnabled > 1) {
    gConfig.mockEnabled = 0;
    changed = true;
  }
  if (gConfig.mockEnabled != 0 && gConfig.altitudeMm == 0) {
    gConfig.altitudeMm = kDefaultAltitudeMm;
    changed = true;
  }
  if (changed) {
    saveConfig();
  }
}

void applyCoreConfig() { gNav.config.demo_force_gps_denied = true; }

void seedMock() {
  nav_mock_init(&gMock);
  const nav_position_t localTruth = {504520000, 305260000, 183500};
  nav_mock_set_local_truth(&gMock, localTruth);
  const nav_mock_peer_t peers[3] = {
      {1u, {504501000, 305234000, 180000}, true, -60, 10, 100u},
      {2u, {504565000, 305201000, 190000}, true, -62, 9, 100u},
      {3u, {504510000, 305340000, 175000}, true, -64, 8, 100u},
  };
  for (const auto &peer : peers) {
    nav_mock_add_peer(&gMock, &peer);
  }
  nav_mock_set_enabled(&gMock, kMockSourceAvailable && gConfig.mockEnabled != 0);
}

void emitIntoCore(const nav_event_t *event, void *user) {
  nav_core_handle_event(static_cast<nav_system_t *>(user), event);
}

bool isValidNodeId(uint8_t nodeId) { return nodeId < NAV_MAX_NODES; }

uint32_t rangingAddressFor(uint8_t slaveNodeId) {
  return kRangingAddressBase | static_cast<uint32_t>(slaveNodeId);
}

uint8_t nextPeerAfter(uint8_t selfId, uint8_t previousPeerId) {
  for (uint8_t offset = 1; offset <= NAV_MAX_NODES; ++offset) {
    const uint8_t candidate = static_cast<uint8_t>((previousPeerId + offset) % NAV_MAX_NODES);
    if (candidate != selfId) {
      return candidate;
    }
  }
  return NAV_INVALID_NODE_ID;
}

uint32_t initialMasterAt(uint8_t nodeId) {
  return millis() + kInitialMasterDelayMs + (static_cast<uint32_t>(nodeId) * kMasterTurnSpacingMs);
}

bool timeReached(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

uint32_t listenWindowUntil(uint32_t now, uint32_t deadline) {
  if (timeReached(now, deadline)) {
    return 0;
  }
  const uint32_t untilDeadlineMs = deadline - now;
  return untilDeadlineMs < kSlaveListenWindowMs ? untilDeadlineMs : kSlaveListenWindowMs;
}

uint32_t remainingInWindow(uint32_t startedMs, uint32_t windowMs) {
  const uint32_t elapsedMs = millis() - startedMs;
  return elapsedMs >= windowMs ? 0 : windowMs - elapsedMs;
}

RangeCorrection correctRange(float biasedRawM) {
  if (biasedRawM <= kShortRangeCorrectionLimitM) {
    return {expf((biasedRawM + 2.4917f) / 7.2262f), "short_exp"};
  }
  return {biasedRawM, "linear_bias"};
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
  if (name == nullptr) return NAV_RANGE_FAIL_UNKNOWN;
  if (strcmp(name, "TIMEOUT") == 0) return NAV_RANGE_FAIL_TIMEOUT;
  if (strcmp(name, "NO_RESPONSE") == 0) return NAV_RANGE_FAIL_NO_RESPONSE;
  if (strcmp(name, "RADIO_BUSY") == 0) return NAV_RANGE_FAIL_RADIO_BUSY;
  if (strcmp(name, "BAD_FRAME") == 0) return NAV_RANGE_FAIL_BAD_FRAME;
  if (strcmp(name, "RANGING_ENGINE_ERROR") == 0) return NAV_RANGE_FAIL_RANGING_ENGINE_ERROR;
  if (strcmp(name, "ABORTED") == 0) return NAV_RANGE_FAIL_ABORTED;
  if (strcmp(name, "NONE") == 0) return NAV_RANGE_FAIL_NONE;
  return NAV_RANGE_FAIL_UNKNOWN;
}

void setPaMode(PaMode mode) {
  switch (mode) {
    case PA_RX:
      digitalWrite(PIN_PA_RXEN, HIGH);
      digitalWrite(PIN_PA_TXEN, LOW);
      break;
    case PA_TX:
      digitalWrite(PIN_PA_RXEN, LOW);
      digitalWrite(PIN_PA_TXEN, HIGH);
      break;
    default:
      digitalWrite(PIN_PA_RXEN, LOW);
      digitalWrite(PIN_PA_TXEN, LOW);
      break;
  }
}

void initCoreForConfig() {
  nav_config_t cfg = nav_config_default(gConfig.nodeId);
  cfg.telemetry_ttl_ms = kHardwareTelemetryTtlMs;
  cfg.range_ttl_ms = kHardwareRangeTtlMs;
  cfg.local_altitude_ttl_ms = kHardwareLocalAltitudeTtlMs;
  cfg.min_anchor_triangle_area_m2 = 10.0f;
  cfg.degraded_anchor_triangle_area_m2 = 100.0f;
  nav_core_init(&gNav, &cfg);
  applyCoreConfig();
  memset(gNodeQuality, 0, sizeof(gNodeQuality));
  seedMock();
}

void printRecord(const char *buf, int written, size_t cap) {
  if (written >= 0 && static_cast<size_t>(written) < cap) {
    Serial.println(buf);
  }
}

void emitLog(uint32_t now, const char *level, const char *tag, const char *text) {
  char buf[256];
  const int written = nav_serial_write_log_record(buf, sizeof(buf), now, level, tag, text);
  printRecord(buf, written, sizeof(buf));
}

uint32_t metersToMillimeters(float valueM) {
  if (!isfinite(valueM) || valueM <= 0.0f) {
    return 0u;
  }
  if (valueM >= 4294967.0f) {
    return UINT32_MAX;
  }
  return static_cast<uint32_t>((valueM * 1000.0f) + 0.5f);
}

void fillLocalNodeQualityReport(uint32_t packetSeq,
                                const nav_snapshot_t &snapshot,
                                nav_node_quality_report_t *out) {
  memset(out, 0, sizeof(*out));
  out->node_id = snapshot.node_id;
  out->nav_mode = snapshot.nav_mode;
  out->solution_status = snapshot.solution_status;
  out->solution_source = snapshot.solution_source;
  out->num_anchors = snapshot.num_anchors;
  for (size_t i = 0; i < NAV_TRILAT_ANCHOR_COUNT; ++i) {
    out->anchor_ids[i] = snapshot.selected_anchor_node_ids[i];
  }
  out->fix_type = NAV_GNSS_FIX_NONE;
  out->geometry_score = snapshot.geometry_score;
  out->total_quality = snapshot.total_quality;
  out->residual_rms_mm = metersToMillimeters(snapshot.residual_rms_m);
  out->max_residual_mm = metersToMillimeters(snapshot.max_residual_m);
  out->hacc_mm = snapshot.hacc_mm;
  out->vacc_mm = snapshot.vacc_mm;
  out->position = snapshot.position;
  out->packet_seq = packetSeq;
}

bool remoteDebugActive(uint32_t now) {
  return gRemoteDebugActiveUntilMs != 0 && !timeReached(now, gRemoteDebugActiveUntilMs);
}

bool periodDue(uint32_t now, uint32_t lastMs, uint32_t periodMs) {
  return lastMs == 0 || (now - lastMs) >= periodMs;
}

bool getCurrentLocalQuality(uint32_t packetSeq, nav_node_quality_report_t *out) {
  nav_snapshot_t snapshot;
  if (out == nullptr || !nav_core_get_snapshot(&gNav, &snapshot)) {
    return false;
  }
  fillLocalNodeQualityReport(packetSeq, snapshot, out);
  return true;
}

void irqFlagsToString(uint16_t irq, char *buffer, size_t bufferSize) {
  if (bufferSize == 0) return;
  buffer[0] = '\0';
  auto append = [&](const char *flag) {
    const size_t used = strlen(buffer);
    if (used + 2 >= bufferSize) return;
    if (used > 0) {
      strncat(buffer, " ", bufferSize - strlen(buffer) - 1);
    }
    strncat(buffer, flag, bufferSize - strlen(buffer) - 1);
  };
  if (irq & SX1280::IRQ_RANGING_SLAVE_REQUEST_VALID) append("slave_req_valid");
  if (irq & SX1280::IRQ_RANGING_MASTER_TIMEOUT) append("master_timeout");
  if (irq & SX1280::IRQ_RANGING_MASTER_RESULT_VALID) append("master_result_valid");
  if (irq & SX1280::IRQ_RANGING_SLAVE_REQUEST_DISCARD) append("slave_req_discard");
  if (irq & SX1280::IRQ_RANGING_SLAVE_RESPONSE_DONE) append("slave_response_done");
  if (irq & SX1280::IRQ_RX_TX_TIMEOUT) append("rx_tx_timeout");
}

bool emitRangeRecord(const nav_serial_range_record_t *record) {
  if (record == nullptr) return false;
  char buf[512];
  const int written = nav_serial_write_range_record(buf, sizeof(buf), record);
  if (written < 0 || static_cast<size_t>(written) >= sizeof(buf)) {
    return false;
  }
  Serial.println(buf);
  return true;
}

void injectRangeResult(uint8_t fromId,
                       uint8_t peerId,
                       uint16_t requestId,
                       uint32_t rangeMm,
                       int16_t rssiDbm,
                       int16_t snrDb,
                       bool valid) {
  const uint32_t now = millis();
  nav_event_t event = {};
  event.type = NAV_EVT_RANGE_RESULT;
  event.timestamp_ms = now;
  event.data.range_result.peer_id = peerId;
  event.data.range_result.request_id = requestId;
  event.data.range_result.timestamp_ms = now;
  event.data.range_result.range_mm = rangeMm;
  event.data.range_result.range_sigma_mm = kRangeSigmaMm;
  event.data.range_result.rssi_dbm = rssiDbm;
  event.data.range_result.snr_db = snrDb;
  event.data.range_result.valid = valid;
  nav_core_handle_event(&gNav, &event);

  nav_serial_range_record_t record = {};
  record.timestamp_ms = now;
  record.from_id = fromId;
  record.to_id = peerId;
  record.request_id = requestId;
  record.ok = valid;
  record.range_mm = rangeMm;
  record.range_sigma_mm = kRangeSigmaMm;
  record.rssi_dbm = rssiDbm;
  record.snr_db = snrDb;
  record.range_fail_reason = valid ? NAV_RANGE_FAIL_NONE : NAV_RANGE_FAIL_UNKNOWN;
  record.source = "log";
  (void)emitRangeRecord(&record);
}

void injectRangeFail(uint8_t fromId, uint8_t peerId, uint16_t requestId, nav_range_fail_reason_t reason) {
  const uint32_t now = millis();
  nav_event_t event = {};
  event.type = NAV_EVT_RANGE_FAIL;
  event.timestamp_ms = now;
  event.data.range_failure.peer_id = peerId;
  event.data.range_failure.request_id = requestId;
  event.data.range_failure.timestamp_ms = now;
  event.data.range_failure.reason = reason;
  nav_core_handle_event(&gNav, &event);

  nav_serial_range_record_t record = {};
  record.timestamp_ms = now;
  record.from_id = fromId;
  record.to_id = peerId;
  record.request_id = requestId;
  record.ok = false;
  record.range_fail_reason = reason;
  record.source = "log";
  (void)emitRangeRecord(&record);
}

void broadcastRangeReport(const char *rangeResultText) {
  if (rangeResultText == nullptr || rangeResultText[0] == '\0' || !gRadioReady) {
    return;
  }

  char payload[kRangeReportPayloadMax + 1];
  const int written = snprintf(payload, sizeof(payload), "NRR1 %s", rangeResultText);
  if (written <= 0 || static_cast<size_t>(written) >= sizeof(payload)) {
    emitLog(millis(), "WARN", "RANGE", "range_report tx failed: payload too long");
    return;
  }

  const uint8_t payloadLen = static_cast<uint8_t>(strlen(payload));
  for (uint8_t attempt = 0; attempt < kRangeReportTxRepeats; ++attempt) {
    setPaMode(PA_TX);
    delayMicroseconds(3000);
    const bool ok = gRadio.transmitPacket(reinterpret_cast<const uint8_t *>(payload), payloadLen, kPacketTxTimeoutMs);
    setPaMode(PA_OFF);
    if (!ok) {
      emitLog(millis(), "WARN", "RANGE", "range_report tx failed");
      return;
    }
    if ((attempt + 1) < kRangeReportTxRepeats) {
      delay(kRangeReportTxGapMs);
    }
  }
}

void sanitizeReportText(char *text) {
  if (text == nullptr) return;
  for (char *p = text; *p != '\0'; ++p) {
    const unsigned char c = static_cast<unsigned char>(*p);
    if (c < 32 || c > 126) {
      *p = ' ';
    }
  }
}

bool parseLegacyRangeReport(const char *text,
                            uint32_t timestampMs,
                            int16_t rssiDbm,
                            int16_t snrDb,
                            nav_serial_range_record_t *out) {
  if (text == nullptr || out == nullptr) return false;

  unsigned fromId = 0;
  unsigned toId = 0;
  unsigned requestId = 0;
  unsigned long rangeMm = 0;
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

void initRadio() {
  pinMode(PIN_PA_RXEN, OUTPUT);
  pinMode(PIN_PA_TXEN, OUTPUT);
  pinMode(PIN_RADIO_DIO1, INPUT);
  setPaMode(PA_OFF);

  emitLog(millis(), "INFO", "RADIO", "initializing SpeedyBee SX1280 ranging");
  gRadioReady = gRadio.beginRanging(SX1280::RANGING_ROLE_SLAVE,
                                    kRadioFrequencyHz,
                                    rangingAddressFor(gConfig.nodeId),
                                    kRangingCalibration);
  if (gRadioReady) {
    emitLog(millis(), "OK", "RADIO", "SpeedyBee SX1280 ranging initialized");
  } else {
    emitLog(millis(), "ERROR", "RADIO", "SpeedyBee SX1280 ranging init failed");
  }
}

void handleDebugEnableFrame(const nav_radio_frame_t &frame, uint32_t now) {
  nav_debug_enable_t debug;
  if (nav_telemetry_decode_debug_enable(&frame, &debug) != NAV_STATUS_OK) {
    emitLog(now, "WARN", "RADIO", "debug_enable decode failed");
    return;
  }
  const bool wasActive = remoteDebugActive(now);
  const uint32_t ttlMs = debug.ttl_ms == 0 ? kRemoteDebugFallbackTtlMs : debug.ttl_ms;
  gRemoteDebugActiveUntilMs = now + ttlMs;
  gLastNodeQualityTxMs = 0;
  gRemoteDebugWasActive = true;
  if (!wasActive) {
    emitLog(now, "INFO", "RADIO", "debug telemetry enabled by OTA");
  }
}

void handleBeaconFrame(const uint8_t *payload, uint8_t len, uint32_t now, const SX1280::PacketStatus &packetStatus) {
  nav_event_t event = {};
  const nav_status_t status = nav_telemetry_decode_event(payload,
                                                         len,
                                                         now,
                                                         packetStatus.rssi_dbm,
                                                         static_cast<int16_t>(lroundf(packetStatus.snr_db)),
                                                         &event);
  if (status != NAV_STATUS_OK || event.type != NAV_EVT_PEER_TELEMETRY_RX) {
    emitLog(now, "WARN", "RADIO", "beacon decode failed");
    return;
  }
  const uint8_t peerId = event.data.peer_beacon_rx.telemetry.node_id;
  if (peerId == gConfig.nodeId) {
    return;
  }
  nav_core_handle_event(&gNav, &event);
}

void handleRadioFrame(const uint8_t *payload, uint8_t len, uint32_t now, const SX1280::PacketStatus &packetStatus) {
  nav_radio_frame_t frame;
  if (nav_radio_decode_frame(payload, len, &frame) != NAV_STATUS_OK) {
    return;
  }
  switch (frame.type) {
    case NAV_RADIO_MSG_BEACON_RX:
      handleBeaconFrame(payload, len, now, packetStatus);
      break;
    case NAV_RADIO_MSG_DEBUG_ENABLE:
      handleDebugEnableFrame(frame, now);
      break;
    case NAV_RADIO_MSG_NODE_QUALITY_REPORT: {
      nav_node_quality_report_t report = {};
      if (nav_telemetry_decode_node_quality_report(&frame, &report) != NAV_STATUS_OK ||
          report.node_id >= NAV_MAX_NODES) {
        emitLog(now, "WARN", "RADIO", "node_quality decode failed");
        break;
      }
      gNodeQuality[report.node_id].present = true;
      gNodeQuality[report.node_id].report = report;
      gNodeQuality[report.node_id].receivedMs = now;
      break;
    }
    default:
      break;
  }
}

void servicePacketRx(uint32_t now, uint32_t windowMs) {
  if (!gRadioReady || windowMs < 5) {
    return;
  }

  uint8_t payload[NAV_RADIO_MAX_FRAME_BYTES + 6u];
  uint8_t len = 0;
  SX1280::PacketStatus status;
  setPaMode(PA_RX);
  const bool ok = gRadio.receivePacket(payload, sizeof(payload), &len, windowMs, &status);
  setPaMode(PA_OFF);
  if (!ok || len == 0) {
    return;
  }

  if (len >= 5 && memcmp(payload, "NRR1 ", 5) == 0) {
    char text[kRangeReportPayloadMax + 1] = {};
    const uint8_t copyLen = len < kRangeReportPayloadMax ? len : kRangeReportPayloadMax;
    memcpy(text, payload, copyLen);
    text[copyLen] = '\0';
    sanitizeReportText(text);

    char logText[384];
    snprintf(logText,
             sizeof(logText),
             "%s source=air_report heard_by=%u report_rssi_dbm=%d report_snr_db=%.1f",
             text + 5,
             static_cast<unsigned>(gConfig.nodeId),
             static_cast<int>(status.rssi_dbm),
             static_cast<double>(status.snr_db));
    emitLog(now, "INFO", "RANGE", logText);

    nav_serial_range_record_t rangeRecord = {};
    if (parseLegacyRangeReport(text + 5,
                               now,
                               status.rssi_dbm,
                               static_cast<int16_t>(lroundf(status.snr_db)),
                               &rangeRecord)) {
      (void)emitRangeRecord(&rangeRecord);
    }
    return;
  }

  handleRadioFrame(payload, len, millis(), status);
}

void transmitDebugEnable(uint32_t now) {
  nav_debug_enable_t debug = {};
  debug.origin_node_id = gConfig.nodeId;
  debug.ttl_ms = kDebugEnableTtlMs;

  uint8_t frame[NAV_RADIO_MAX_FRAME_BYTES + 6u];
  size_t frameLen = 0;
  if (nav_telemetry_encode_debug_enable(&debug, ++gRadioFrameSeq, frame, sizeof(frame), &frameLen) != NAV_STATUS_OK ||
      frameLen == 0 || frameLen > 255u) {
    emitLog(now, "WARN", "RADIO", "debug_enable encode failed");
    return;
  }

  setPaMode(PA_TX);
  delayMicroseconds(3000);
  const bool ok = gRadio.transmitPacket(frame, static_cast<uint8_t>(frameLen), kPacketTxTimeoutMs);
  setPaMode(PA_OFF);
  if (!ok) {
    emitLog(now, "WARN", "RADIO", "debug_enable tx failed");
  }
}

void transmitNodeQuality(uint32_t now) {
  if (!gRadioReady || !periodDue(now, gLastNodeQualityTxMs, kNodeQualityReportPeriodMs)) {
    return;
  }

  nav_node_quality_report_t report;
  if (!getCurrentLocalQuality(++gRadioQualityPacketSeq, &report)) {
    return;
  }

  uint8_t frame[NAV_RADIO_MAX_FRAME_BYTES + 6u];
  size_t frameLen = 0;
  if (nav_telemetry_encode_node_quality_report(
          &report, ++gRadioFrameSeq, frame, sizeof(frame), &frameLen) != NAV_STATUS_OK ||
      frameLen == 0 || frameLen > 255u) {
    emitLog(now, "WARN", "RADIO", "node_quality encode failed");
    return;
  }

  gLastNodeQualityTxMs = now;
  setPaMode(PA_TX);
  delayMicroseconds(3000);
  const bool ok = gRadio.transmitPacket(frame, static_cast<uint8_t>(frameLen), kPacketTxTimeoutMs);
  setPaMode(PA_OFF);
  if (!ok) {
    emitLog(now, "WARN", "RADIO", "node_quality tx failed");
  } else if (periodDue(now, gLastNodeQualityTxLogMs, 5000)) {
    gLastNodeQualityTxLogMs = now;
    emitLog(now, "DEBUG", "RADIO", "node_quality tx ok");
  }
}

void serviceDebugTelemetryTx(uint32_t now, uint32_t remainingMs) {
  if (!gRadioReady) {
    if ((gDebugEnabled || remoteDebugActive(now)) && periodDue(now, gLastRadioNotReadyLogMs, 5000)) {
      gLastRadioNotReadyLogMs = now;
      emitLog(now, "WARN", "RADIO", "debug telemetry radio not ready");
    }
    return;
  }

  if (remainingMs < (kBestEffortTxGuardMs + kPacketRxWindowMs)) {
    return;
  }

  if (gDebugEnabled && periodDue(now, gLastDebugEnableTxMs, kDebugEnablePeriodMs)) {
    gLastDebugEnableTxMs = now;
    transmitDebugEnable(now);
    return;
  }

  const bool remoteActiveNow = remoteDebugActive(now);
  if (!remoteActiveNow && gRemoteDebugWasActive) {
    gRemoteDebugWasActive = false;
    emitLog(now, "INFO", "RADIO", "debug telemetry disabled by OTA timeout");
  }

  if (gDebugEnabled || remoteActiveNow) {
    transmitNodeQuality(now);
  }
}

void pumpSerial();
void serviceControlChannel();

void logMasterFailure(uint8_t masterId,
                      uint8_t peerId,
                      uint16_t requestId,
                      uint32_t elapsedMs,
                      nav_range_fail_reason_t reason,
                      uint16_t irq,
                      const char *note) {
  char flags[96];
  irqFlagsToString(irq, flags, sizeof(flags));

  char text[256];
  snprintf(text,
           sizeof(text),
           "range_result ok=false from=%u to=%u request_id=%u range_fail_reason=%s elapsed_ms=%lu irq=0x%04X flags=\"%s\" note=\"%s\"",
           static_cast<unsigned>(masterId),
           static_cast<unsigned>(peerId),
           static_cast<unsigned>(requestId),
           rangeFailReasonName(reason),
           static_cast<unsigned long>(elapsedMs),
           irq,
           flags,
           note);
  emitLog(millis(), "WARN", "RANGE", text);

  // Failed local attempts are already emitted over USB. Avoid switching back to
  // packet TX while recovering from a ranging timeout.
  injectRangeFail(masterId, peerId, requestId, reason);
}

void runMasterExchange(uint8_t masterId, uint8_t peerId) {
  if (!isValidNodeId(masterId) || !isValidNodeId(peerId) || masterId == peerId) {
    return;
  }

  const uint16_t requestId = ++gRangeRequestId;
  const uint32_t startedMs = millis();
  const uint32_t address = rangingAddressFor(peerId);
  if (!gRadio.beginRanging(SX1280::RANGING_ROLE_MASTER, kRadioFrequencyHz, address, kRangingCalibration)) {
    logMasterFailure(masterId,
                     peerId,
                     requestId,
                     millis() - startedMs,
                     NAV_RANGE_FAIL_RANGING_ENGINE_ERROR,
                     gRadio.getIrqStatus(),
                     "beginRanging master failed");
    return;
  }

  setPaMode(PA_TX);
  gRadio.startMasterExchange(kMasterExchangeTimeoutMs);
  delayMicroseconds(3000);
  setPaMode(PA_RX);

  uint16_t irq = 0;
  while ((millis() - startedMs) < kMasterHostTimeoutMs) {
    pumpSerial();
    irq = gRadio.getIrqStatus();
    if (irq & (SX1280::IRQ_RANGING_MASTER_RESULT_VALID |
               SX1280::IRQ_RANGING_MASTER_TIMEOUT |
               SX1280::IRQ_RX_TX_TIMEOUT)) {
      break;
    }
    delay(2);
  }

  const uint32_t elapsedMs = millis() - startedMs;
  if (!(irq & SX1280::IRQ_RANGING_MASTER_RESULT_VALID)) {
    const nav_range_fail_reason_t reason =
        (irq & (SX1280::IRQ_RANGING_MASTER_TIMEOUT | SX1280::IRQ_RX_TX_TIMEOUT))
            ? NAV_RANGE_FAIL_TIMEOUT
            : NAV_RANGE_FAIL_NO_RESPONSE;
    gRadio.clearIrqStatus();
    gRadio.setStandby();
    setPaMode(PA_OFF);
    logMasterFailure(masterId, peerId, requestId, elapsedMs, reason, irq, "ranging timeout");
    return;
  }

  const SX1280::PacketStatus status = gRadio.readPacketStatus();
  const int32_t rawResult = gRadio.readRangingResultRaw();
  const float rawM = gRadio.rawRangingMeters(rawResult);
  const float biasedRawM = rawM + kShortRangeRawBiasM;
  const RangeCorrection correction = correctRange(biasedRawM);
  const bool valid = isfinite(correction.correctedM) && correction.correctedM > 0.0f;
  const uint32_t rangeMm = valid ? metersToMillimeters(correction.correctedM) : 0;
  char flags[96];
  irqFlagsToString(irq, flags, sizeof(flags));

  if (!valid || rangeMm == 0) {
    char text[512];
    snprintf(text,
             sizeof(text),
             "range_result ok=false from=%u to=%u request_id=%u range_fail_reason=%s raw_reg=%ld raw_m=%.2f corrected_m=%.2f elapsed_ms=%lu irq=0x%04X flags=\"%s\" note=\"invalid distance\"",
             static_cast<unsigned>(masterId),
             static_cast<unsigned>(peerId),
             static_cast<unsigned>(requestId),
             rangeFailReasonName(NAV_RANGE_FAIL_RANGING_ENGINE_ERROR),
             static_cast<long>(rawResult),
             static_cast<double>(rawM),
             static_cast<double>(correction.correctedM),
             static_cast<unsigned long>(elapsedMs),
             irq,
             flags);
    emitLog(millis(), "WARN", "RANGE", text);
    injectRangeFail(masterId, peerId, requestId, NAV_RANGE_FAIL_RANGING_ENGINE_ERROR);
    gRadio.clearIrqStatus();
    gRadio.setStandby();
    setPaMode(PA_OFF);
    return;
  }

  char text[512];
  snprintf(text,
           sizeof(text),
           "range_result ok=true from=%u to=%u request_id=%u range_mm=%lu corrected_m=%.2f correction=%s raw_m=%.2f raw_reg=%ld rssi_dbm=%d snr_db=%.1f elapsed_ms=%lu irq=0x%04X flags=\"%s\"",
           static_cast<unsigned>(masterId),
           static_cast<unsigned>(peerId),
           static_cast<unsigned>(requestId),
           static_cast<unsigned long>(rangeMm),
           static_cast<double>(correction.correctedM),
           correction.mode,
           static_cast<double>(rawM),
           static_cast<long>(rawResult),
           static_cast<int>(status.rssi_dbm),
           static_cast<double>(status.snr_db),
           static_cast<unsigned long>(elapsedMs),
           irq,
           flags);
  emitLog(millis(), "OK", "RANGE", text);

  char report[kRangeReportPayloadMax];
  snprintf(report,
           sizeof(report),
           "range_result ok=true from=%u to=%u request_id=%u range_mm=%lu corrected_m=%.2f raw_m=%.2f raw_reg=%ld rssi_dbm=%d snr_db=%.1f elapsed_ms=%lu note=\"range ok\"",
           static_cast<unsigned>(masterId),
           static_cast<unsigned>(peerId),
           static_cast<unsigned>(requestId),
           static_cast<unsigned long>(rangeMm),
           static_cast<double>(correction.correctedM),
           static_cast<double>(rawM),
           static_cast<long>(rawResult),
           static_cast<int>(status.rssi_dbm),
           static_cast<double>(status.snr_db),
           static_cast<unsigned long>(elapsedMs));
  broadcastRangeReport(report);
  injectRangeResult(masterId,
                    peerId,
                    requestId,
                    rangeMm,
                    status.rssi_dbm,
                    static_cast<int16_t>(lroundf(status.snr_db)),
                    true);

  gRadio.clearIrqStatus();
  gRadio.setStandby();
  setPaMode(PA_OFF);
}

void serviceSlave(uint8_t nodeId, uint32_t listenWindowMs) {
  if (!isValidNodeId(nodeId) || listenWindowMs < 10) {
    return;
  }

  const uint32_t startedMs = millis();
  const uint32_t address = rangingAddressFor(nodeId);
  if (!gRadio.beginRanging(SX1280::RANGING_ROLE_SLAVE, kRadioFrequencyHz, address, kRangingCalibration)) {
    emitLog(millis(), "WARN", "RANGE", "slave_listen failed: beginRanging slave failed");
    setPaMode(PA_OFF);
    return;
  }

  setPaMode(PA_RX);
  gRadio.startSlaveListen();
  bool requestLogged = false;
  while ((millis() - startedMs) < listenWindowMs) {
    pumpSerial();
    const uint16_t irq = gRadio.getIrqStatus();
    if ((irq & SX1280::IRQ_RANGING_SLAVE_REQUEST_VALID) && !requestLogged) {
      requestLogged = true;
      gRadio.clearIrqStatus(SX1280::IRQ_RANGING_SLAVE_REQUEST_VALID);
    }
    const uint16_t terminal = SX1280::IRQ_RANGING_SLAVE_RESPONSE_DONE |
                              SX1280::IRQ_RANGING_SLAVE_REQUEST_DISCARD |
                              SX1280::IRQ_RX_TX_TIMEOUT;
    if (irq & terminal) {
      if (irq & SX1280::IRQ_RANGING_SLAVE_RESPONSE_DONE) {
        char flags[96];
        irqFlagsToString(irq, flags, sizeof(flags));
        char text[256];
        snprintf(text,
                 sizeof(text),
                 "slave_response ok=true node=%u address=0x%08lX elapsed_ms=%lu irq=0x%04X flags=\"%s\"",
                 static_cast<unsigned>(nodeId),
                 static_cast<unsigned long>(address),
                 static_cast<unsigned long>(millis() - startedMs),
                 irq,
                 flags);
        emitLog(millis(), "OK", "RANGE", text);
      }
      break;
    }
    delay(10);
  }

  gRadio.clearIrqStatus();
  gRadio.setStandby();
  setPaMode(PA_OFF);
}

void serviceNetworkListen(uint8_t nodeId, uint32_t listenWindowMs) {
  const uint32_t startedMs = millis();
  while ((millis() - startedMs) < listenWindowMs) {
    serviceControlChannel();
    uint32_t remainingMs = remainingInWindow(startedMs, listenWindowMs);
    serviceDebugTelemetryTx(millis(), remainingMs);
    remainingMs = remainingInWindow(startedMs, listenWindowMs);
    if (remainingMs >= (kPacketRxWindowMs + 20)) {
      servicePacketRx(millis(), kPacketRxWindowMs);
      remainingMs = remainingInWindow(startedMs, listenWindowMs);
    }
    if (remainingMs < 20) {
      break;
    }
    const uint32_t slaveWindowMs = remainingMs < kSlaveListenSliceMs ? remainingMs : kSlaveListenSliceMs;
    serviceSlave(nodeId, slaveWindowMs);
  }
}

void serviceRadio(uint32_t now) {
  if (!gRadioReady) {
    if ((gDebugEnabled || remoteDebugActive(now)) && periodDue(now, gLastRadioNotReadyLogMs, 5000)) {
      gLastRadioNotReadyLogMs = now;
      emitLog(now, "WARN", "RADIO", "radio not ready");
    }
    return;
  }
  if (!isValidNodeId(gConfig.nodeId)) {
    return;
  }
  if (gConfig.nodeId != gRadioNodeId) {
    gRadioNodeId = gConfig.nodeId;
    gNextPeerId = nextPeerAfter(gConfig.nodeId, gConfig.nodeId);
    gNextMasterAtMs = initialMasterAt(gConfig.nodeId);
    char text[128];
    snprintf(text,
             sizeof(text),
             "distance-only role node=%u role=single-hop-discovery next_peer=%u",
             static_cast<unsigned>(gConfig.nodeId),
             static_cast<unsigned>(gNextPeerId));
    emitLog(now, "INFO", "RANGE", text);
  }

  if (timeReached(now, gNextMasterAtMs)) {
    if (gNextPeerId != NAV_INVALID_NODE_ID) {
      runMasterExchange(gConfig.nodeId, gNextPeerId);
      gNextPeerId = nextPeerAfter(gConfig.nodeId, gNextPeerId);
    }
    gNextMasterAtMs = millis() + kMasterScanIntervalMs;
    delay(kMasterPeerGapMs);
    return;
  }

  const uint32_t listenWindowMs = listenWindowUntil(now, gNextMasterAtMs);
  if (listenWindowMs >= 20) {
    serviceNetworkListen(gConfig.nodeId, listenWindowMs);
  }
}

void applyCommand(const nav_ctrl_command_t &cmd) {
  bool changed = false;
  const uint32_t now = millis();
  switch (cmd.type) {
    case NAV_CTRL_CMD_SET_NAME:
      strncpy(gConfig.name, cmd.str_value, sizeof(gConfig.name) - 1);
      gConfig.name[sizeof(gConfig.name) - 1] = '\0';
      changed = true;
      emitLog(now, "INFO", "CONFIG", "node name updated");
      break;
    case NAV_CTRL_CMD_SET_GPS:
      gConfig.gpsEnabled = 0;
      applyCoreConfig();
      emitLog(now, "INFO", "CONFIG", "GPS command ignored: SpeedyBee has no GPS hardware");
      break;
    case NAV_CTRL_CMD_SET_MOCK:
      if (kMockSourceAvailable) {
        gConfig.mockEnabled = cmd.bool_value ? 1 : 0;
        nav_mock_set_enabled(&gMock, gConfig.mockEnabled != 0);
        changed = true;
        emitLog(now, "INFO", "CONFIG", gConfig.mockEnabled != 0 ? "mock enabled" : "mock disabled");
      } else {
        gConfig.mockEnabled = 0;
        nav_mock_set_enabled(&gMock, false);
        emitLog(now, "INFO", "CONFIG", "mock command ignored: disabled in SpeedyBee build");
      }
      break;
    case NAV_CTRL_CMD_SET_ALTITUDE:
      gConfig.altitudeMm = cmd.int_value;
      changed = true;
      emitLog(now, "INFO", "CONFIG", "altitude updated");
      break;
    case NAV_CTRL_CMD_SET_NODE_ID:
      if (cmd.int_value >= 0 && cmd.int_value < static_cast<int32_t>(NAV_MAX_NODES)) {
        gConfig.nodeId = static_cast<uint8_t>(cmd.int_value);
        snprintf(gConfig.name, sizeof(gConfig.name), "speedybee-%u", gConfig.nodeId);
        initCoreForConfig();
        changed = true;
        emitLog(now, "INFO", "CONFIG", "node id updated");
      }
      break;
    case NAV_CTRL_CMD_SET_DEBUG:
      gDebugEnabled = cmd.bool_value;
      if (!gDebugEnabled) {
        memset(gNodeQuality, 0, sizeof(gNodeQuality));
      }
      if (gDebugEnabled) {
        emitLog(now,
                "INFO",
                "CONFIG",
                gRadioReady ? "debug telemetry enabled; radio ready"
                            : "debug telemetry enabled; radio not ready");
      } else {
        emitLog(now, "INFO", "CONFIG", "debug telemetry disabled");
      }
      break;
    default:
      break;
  }
  if (changed) saveConfig();
}

void handleLine(char *line) {
  nav_ctrl_command_t cmd;
  if (nav_serial_parse_command(line, &cmd) == NAV_STATUS_OK) {
    applyCommand(cmd);
  }
}

void pumpSerial() {
  while (Serial.available() > 0) {
    const int c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (gLineLen > 0) {
        gLine[gLineLen] = '\0';
        handleLine(gLine);
        gLineLen = 0;
      }
    } else if (gLineLen + 1 < sizeof(gLine)) {
      gLine[gLineLen++] = static_cast<char>(c);
    } else {
      gLineLen = 0;
    }
  }
}

void feedAltitude(uint32_t now) {
  nav_event_t altitude = {};
  altitude.type = NAV_EVT_LOCAL_ALTITUDE_SAMPLE;
  altitude.timestamp_ms = now;
  altitude.data.local_altitude.alt_mm = gConfig.altitudeMm;
  altitude.data.local_altitude.timestamp_ms = now;
  altitude.data.local_altitude.source = NAV_ALT_SOURCE_MANUAL;
  altitude.data.local_altitude.valid = true;
  nav_core_handle_event(&gNav, &altitude);
}

void emitRecords(uint32_t now) {
  feedAltitude(now);
  nav_mock_emit(&gMock, now, emitIntoCore, &gNav);
  nav_core_tick(&gNav, now);

  nav_snapshot_t snapshot;
  if (!nav_core_get_snapshot(&gNav, &snapshot)) {
    return;
  }
  nav_serial_node_info_t info;
  info.node_id = gConfig.nodeId;
  info.node_name = gConfig.name;
  info.gps_enabled = false;
  info.mock_enabled = kMockSourceAvailable && gConfig.mockEnabled != 0;

  static char buf[kRecordBufMax];
  int written = nav_serial_write_snapshot_record(buf, sizeof(buf), &info, &snapshot, &gNav.peer_table);
  printRecord(buf, written, sizeof(buf));

  if (gDebugEnabled) {
    nav_node_quality_report_t quality;
    fillLocalNodeQualityReport(++gLocalQualityPacketSeq, snapshot, &quality);
    written = nav_serial_write_node_quality_record(buf, sizeof(buf), &quality, now, 0u, "local");
    printRecord(buf, written, sizeof(buf));
    for (uint8_t i = 0; i < NAV_MAX_NODES; ++i) {
      if (!gNodeQuality[i].present) {
        continue;
      }
      const uint32_t ageMs = now - gNodeQuality[i].receivedMs;
      written = nav_serial_write_node_quality_record(buf, sizeof(buf), &gNodeQuality[i].report, now, ageMs, "peer");
      printRecord(buf, written, sizeof(buf));
    }
  }
}

void serviceControlChannel() {
  pumpSerial();
  const uint32_t now = millis();
  if (now - gLastSnapshotMs >= kSnapshotPeriodMs) {
    gLastSnapshotMs = now;
    emitRecords(now);
  }
}

}  // namespace

void setup() {
  Serial.begin(kBaud);
  EEPROM.begin(sizeof(PersistConfig) + 8);
  loadConfig();
  initCoreForConfig();
  emitLog(millis(), "INFO", "SYSTEM", "SpeedyBee firmware started");
  initRadio();
}

void loop() {
  serviceControlChannel();
  serviceRadio(millis());
}
