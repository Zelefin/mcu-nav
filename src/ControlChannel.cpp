#include "ControlChannel.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_timer.h"

#include "Logger.h"
#include "nav/nav_core.h"
#include "nav/nav_gnss.h"
#include "nav/nav_mock.h"
#include "nav/nav_quality.h"
#include "nav/nav_serial_json.h"
#include "nav/nav_state_machine.h"

#ifndef NAV_ENABLE_GNSS
#define NAV_ENABLE_GNSS 1
#endif

namespace {

constexpr uint32_t kSnapshotPeriodMs = 500u;
constexpr uint32_t kHardwareTelemetryTtlMs = 3000u;
constexpr uint32_t kHardwareRangeTtlMs = 15000u;
constexpr uint32_t kHardwareLocalAltitudeTtlMs = 2000u;
constexpr size_t kLineMax = 256u;
constexpr size_t kRecordBufMax = 1536u;

struct BufferedNodeQuality {
  bool present;
  nav_node_quality_report_t report;
  uint32_t receivedMs;
};

NodeConfig gConfig;
nav_system_t gNav;
nav_mock_t gMock;
SemaphoreHandle_t gMutex = nullptr;
bool gDebugEnabled = false;
uint32_t gLocalQualityPacketSeq = 0u;
BufferedNodeQuality gNodeQuality[NAV_MAX_NODES];

void emitIntoCore(const nav_event_t *event, void *user) {
  nav_core_handle_event(static_cast<nav_system_t *>(user), event);
}

uint32_t nowMs() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

void seedMock();

bool effectiveGpsEnabled() {
#if NAV_ENABLE_GNSS
  return gConfig.gpsEnabled;
#else
  return false;
#endif
}

void applyCoreConfig() {
  gNav.config.demo_force_gps_denied = !effectiveGpsEnabled();
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
  std::memset(gNodeQuality, 0, sizeof(gNodeQuality));
  seedMock();
}

uint32_t metersToMillimeters(float valueM) {
  if (!std::isfinite(valueM) || valueM <= 0.0f) {
    return 0u;
  }
  if (valueM >= 4294967.0f) {
    return UINT32_MAX;
  }
  return static_cast<uint32_t>((valueM * 1000.0f) + 0.5f);
}

void fillLocalNodeQualityReportLocked(uint32_t packetSeq,
                                      const nav_snapshot_t &snapshot,
                                      nav_node_quality_report_t *out) {
  std::memset(out, 0, sizeof(*out));
  out->node_id = snapshot.node_id;
  out->nav_mode = snapshot.nav_mode;
  out->solution_status = snapshot.solution_status;
  out->solution_source = snapshot.solution_source;
  out->num_anchors = snapshot.num_anchors;
  for (size_t i = 0; i < NAV_TRILAT_ANCHOR_COUNT; ++i) {
    out->anchor_ids[i] = snapshot.selected_anchor_node_ids[i];
  }
  out->geometry_score = snapshot.geometry_score;
  out->total_quality = snapshot.total_quality;
  out->residual_rms_mm = metersToMillimeters(snapshot.residual_rms_m);
  out->max_residual_mm = metersToMillimeters(snapshot.max_residual_m);
  out->hacc_mm = snapshot.hacc_mm;
  out->vacc_mm = snapshot.vacc_mm;
  out->position = snapshot.position;
  out->packet_seq = packetSeq;

  if (gNav.local_gnss_present) {
    out->fix_type = gNav.local_gnss.fix_type;
    out->satellites = gNav.local_gnss.satellites;
    out->hdop_centi = gNav.local_gnss.hdop_centi;
    out->hacc_mm = gNav.local_gnss.hacc_mm;
    out->vacc_mm = gNav.local_gnss.vacc_mm;
  } else {
    out->fix_type = NAV_GNSS_FIX_NONE;
  }
}

void clearNodeQualityLocked() {
  std::memset(gNodeQuality, 0, sizeof(gNodeQuality));
}

void printRecord(const char *record) {
  if (record == nullptr || record[0] == '\0') {
    return;
  }
  Logger::writeLine(record);
}

// Default debug scene so a lone board has something to trilaterate against when
// mock is enabled. Real peers arrive from the radio driver (future work).
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
  nav_mock_set_enabled(&gMock, gConfig.mockEnabled);
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

void applyCommand(const nav_ctrl_command_t &cmd) {
  bool changed = false;
  switch (cmd.type) {
    case NAV_CTRL_CMD_SET_NAME:
      std::strncpy(gConfig.name, cmd.str_value, sizeof(gConfig.name) - 1);
      gConfig.name[sizeof(gConfig.name) - 1] = '\0';
      changed = true;
      Logger::infof("CONFIG", "node name set to %s", gConfig.name);
      break;
    case NAV_CTRL_CMD_SET_GPS:
#if NAV_ENABLE_GNSS
      gConfig.gpsEnabled = cmd.bool_value;
      applyCoreConfig();
      changed = true;
      Logger::infof("CONFIG", "GPS %s", gConfig.gpsEnabled ? "enabled" : "disabled (trilateration)");
#else
      gConfig.gpsEnabled = false;
      applyCoreConfig();
      changed = true;
      Logger::infof("CONFIG", "GPS command ignored: hardware GPS is disabled in this firmware");
#endif
      break;
    case NAV_CTRL_CMD_SET_MOCK:
      gConfig.mockEnabled = cmd.bool_value;
      nav_mock_set_enabled(&gMock, gConfig.mockEnabled);
      changed = true;
      Logger::infof("CONFIG", "mock %s", gConfig.mockEnabled ? "enabled" : "disabled");
      break;
    case NAV_CTRL_CMD_SET_ALTITUDE:
      gConfig.altitudeMm = cmd.int_value;
      changed = true;
      Logger::infof("CONFIG", "altitude const set to %ld mm", static_cast<long>(gConfig.altitudeMm));
      break;
    case NAV_CTRL_CMD_SET_NODE_ID:
      gConfig.nodeId = static_cast<uint8_t>(cmd.int_value);
      std::snprintf(gConfig.name, sizeof(gConfig.name), "node-%u", static_cast<unsigned>(gConfig.nodeId));
      initCoreForConfig();
      changed = true;
      Logger::infof("CONFIG", "node id set to %u", static_cast<unsigned>(gConfig.nodeId));
      break;
    case NAV_CTRL_CMD_SET_DEBUG:
      gDebugEnabled = cmd.bool_value;
      if (!gDebugEnabled) {
        clearNodeQualityLocked();
      }
      Logger::infof("CONFIG", "debug telemetry %s", gDebugEnabled ? "enabled" : "disabled");
      break;
    case NAV_CTRL_CMD_GET:
    case NAV_CTRL_CMD_UNKNOWN:
    case NAV_CTRL_CMD_NONE:
    default:
      break;
  }
  if (changed && !NodeConfigStore::save(gConfig)) {
    Logger::warnf("CONFIG", "failed to persist config to NVS");
  }
}

void ReaderTask(void *) {
  char line[kLineMax];
  size_t len = 0;
  for (;;) {
    const int c = std::getchar();
    if (c == EOF) {
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    if (c == '\n' || c == '\r') {
      if (len == 0) {
        continue;
      }
      line[len] = '\0';
      len = 0;
      nav_ctrl_command_t cmd;
      if (nav_serial_parse_command(line, &cmd) == NAV_STATUS_OK) {
        xSemaphoreTake(gMutex, portMAX_DELAY);
        applyCommand(cmd);
        xSemaphoreGive(gMutex);
      }
      continue;
    }
    if (len + 1 < sizeof(line)) {
      line[len++] = static_cast<char>(c);
    } else {
      len = 0;  // overflow: drop the malformed line
    }
  }
}

void EmitterTask(void *) {
  char buf[kRecordBufMax];
  for (;;) {
    const uint32_t now = nowMs();
    bool emitDebug = false;
    bool haveLocalQuality = false;
    nav_node_quality_report_t localQuality = {};
    BufferedNodeQuality peerQuality[NAV_MAX_NODES] = {};

    xSemaphoreTake(gMutex, portMAX_DELAY);
    feedAltitude(now);
    nav_mock_emit(&gMock, now, emitIntoCore, &gNav);
    nav_core_tick(&gNav, now);

    nav_snapshot_t snapshot;
    nav_core_get_snapshot(&gNav, &snapshot);
    nav_serial_node_info_t info;
    info.node_id = gConfig.nodeId;
    info.node_name = gConfig.name;
    info.gps_enabled = effectiveGpsEnabled();
    info.mock_enabled = gConfig.mockEnabled;
    nav_serial_write_snapshot_record(buf, sizeof(buf), &info, &snapshot, &gNav.peer_table);
    emitDebug = gDebugEnabled;
    if (emitDebug) {
      fillLocalNodeQualityReportLocked(++gLocalQualityPacketSeq, snapshot, &localQuality);
      haveLocalQuality = true;
      std::memcpy(peerQuality, gNodeQuality, sizeof(peerQuality));
    }
    xSemaphoreGive(gMutex);

    printRecord(buf);
    if (emitDebug && haveLocalQuality) {
      nav_serial_write_node_quality_record(buf, sizeof(buf), &localQuality, now, 0u, "local");
      printRecord(buf);
      for (uint8_t i = 0; i < NAV_MAX_NODES; ++i) {
        if (!peerQuality[i].present) {
          continue;
        }
        const uint32_t ageMs = now - peerQuality[i].receivedMs;
        nav_serial_write_node_quality_record(buf, sizeof(buf), &peerQuality[i].report, now, ageMs, "peer");
        printRecord(buf);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(kSnapshotPeriodMs));
  }
}

}  // namespace

namespace ControlChannel {

void begin(const NodeConfig &config) {
  gConfig = config;
#if !NAV_ENABLE_GNSS
  gConfig.gpsEnabled = false;
#endif

  gMutex = xSemaphoreCreateMutex();
  xSemaphoreTake(gMutex, portMAX_DELAY);
  initCoreForConfig();
  xSemaphoreGive(gMutex);
  xTaskCreate(ReaderTask, "ctrlReader", 4096, nullptr, 2, nullptr);
  xTaskCreate(EmitterTask, "ctrlEmitter", 6144, nullptr, 2, nullptr);
  Logger::infof("CONFIG", "control channel started (node=%s id=%u gps=%d mock=%d)", gConfig.name,
                static_cast<unsigned>(gConfig.nodeId), effectiveGpsEnabled() ? 1 : 0,
                gConfig.mockEnabled ? 1 : 0);
}

bool getConfig(NodeConfig *out) {
  if (out == nullptr || gMutex == nullptr) {
    return false;
  }
  xSemaphoreTake(gMutex, portMAX_DELAY);
  *out = gConfig;
  xSemaphoreGive(gMutex);
  return true;
}

bool isDebugEnabled() {
  if (gMutex == nullptr) {
    return false;
  }
  xSemaphoreTake(gMutex, portMAX_DELAY);
  const bool enabled = gDebugEnabled;
  xSemaphoreGive(gMutex);
  return enabled;
}

bool isGpsEnabled() {
  if (gMutex == nullptr) {
    return false;
  }
  xSemaphoreTake(gMutex, portMAX_DELAY);
  const bool enabled = effectiveGpsEnabled();
  xSemaphoreGive(gMutex);
  return enabled;
}

bool getLocalTelemetry(uint32_t packetSeq, nav_peer_telemetry_t *out) {
  if (out == nullptr || gMutex == nullptr) {
    return false;
  }

  xSemaphoreTake(gMutex, portMAX_DELAY);
  const uint32_t currentMs = nowMs();
  const bool freshGnss =
      gNav.local_gnss_present &&
      (gNav.config.telemetry_ttl_ms == 0u || (currentMs - gNav.local_gnss.timestamp_ms) <= gNav.config.telemetry_ttl_ms);
  const bool usableGnss = effectiveGpsEnabled() && freshGnss && nav_gnss_sample_is_usable(&gNav.local_gnss);
  if (usableGnss) {
    nav_snapshot_t snapshot;
    (void)nav_core_get_snapshot(&gNav, &snapshot);
    std::memset(out, 0, sizeof(*out));
    out->node_id = gConfig.nodeId;
    out->packet_seq = packetSeq;
    out->timestamp_ms = currentMs;
    out->position = gNav.local_gnss.position;
    out->velocity = gNav.local_gnss.velocity;
    out->fix_type = gNav.local_gnss.fix_type;
    out->gnss_valid = true;
    out->satellites = gNav.local_gnss.satellites;
    out->hdop_centi = gNav.local_gnss.hdop_centi;
    out->hacc_mm = gNav.local_gnss.hacc_mm;
    out->vacc_mm = gNav.local_gnss.vacc_mm;
    out->nav_mode = snapshot.nav_mode;
  }
  xSemaphoreGive(gMutex);
  return usableGnss;
}

bool getLocalNodeQualityReport(uint32_t packetSeq, nav_node_quality_report_t *out) {
  if (out == nullptr || gMutex == nullptr) {
    return false;
  }

  xSemaphoreTake(gMutex, portMAX_DELAY);
  nav_snapshot_t snapshot;
  const bool ok = nav_core_get_snapshot(&gNav, &snapshot);
  if (ok) {
    fillLocalNodeQualityReportLocked(packetSeq, snapshot, out);
  }
  xSemaphoreGive(gMutex);
  return ok;
}

bool handleNodeQualityReport(const nav_node_quality_report_t *report, uint32_t receivedMs) {
  if (report == nullptr || gMutex == nullptr || report->node_id >= NAV_MAX_NODES) {
    return false;
  }
  xSemaphoreTake(gMutex, portMAX_DELAY);
  BufferedNodeQuality &slot = gNodeQuality[report->node_id];
  slot.present = true;
  slot.report = *report;
  slot.receivedMs = receivedMs;
  xSemaphoreGive(gMutex);
  return true;
}

bool emitRangeRecord(const nav_serial_range_record_t *record) {
  if (record == nullptr) {
    return false;
  }
  char buf[512];
  const int written = nav_serial_write_range_record(buf, sizeof(buf), record);
  if (written < 0 || static_cast<size_t>(written) >= sizeof(buf)) {
    return false;
  }
  printRecord(buf);
  return true;
}

bool handleEvent(const nav_event_t *event) {
  if (event == nullptr || gMutex == nullptr) {
    return false;
  }
  nav_serial_range_record_t rangeRecord = {};
  bool emitRange = false;
  xSemaphoreTake(gMutex, portMAX_DELAY);
  nav_core_handle_event(&gNav, event);
  if (event->type == NAV_EVT_RANGE_RESULT) {
    rangeRecord.timestamp_ms = event->timestamp_ms;
    rangeRecord.from_id = gConfig.nodeId;
    rangeRecord.to_id = event->data.range_result.peer_id;
    rangeRecord.request_id = event->data.range_result.request_id;
    rangeRecord.ok = event->data.range_result.valid;
    rangeRecord.range_mm = event->data.range_result.range_mm;
    rangeRecord.range_sigma_mm = event->data.range_result.range_sigma_mm;
    rangeRecord.rssi_dbm = event->data.range_result.rssi_dbm;
    rangeRecord.snr_db = event->data.range_result.snr_db;
    rangeRecord.range_fail_reason = event->data.range_result.valid ? NAV_RANGE_FAIL_NONE : NAV_RANGE_FAIL_UNKNOWN;
    rangeRecord.source = "log";
    emitRange = true;
  } else if (event->type == NAV_EVT_RANGE_FAIL) {
    rangeRecord.timestamp_ms = event->timestamp_ms;
    rangeRecord.from_id = gConfig.nodeId;
    rangeRecord.to_id = event->data.range_failure.peer_id;
    rangeRecord.request_id = event->data.range_failure.request_id;
    rangeRecord.ok = false;
    rangeRecord.range_fail_reason = event->data.range_failure.reason;
    rangeRecord.source = "log";
    emitRange = true;
  }
  xSemaphoreGive(gMutex);
  if (emitRange) {
    (void)emitRangeRecord(&rangeRecord);
  }
  return true;
}

}  // namespace ControlChannel
