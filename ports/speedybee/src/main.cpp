// SpeedyBee Nano 2.4G (ESP8285 + SX1280) navigation node firmware.
//
// Reuses the portable nav core over the Arduino/ESP8266 framework (ESP-IDF is
// not available on ESP8285). No GPS/compass on this board, so it defaults to
// trilateration and is the ideal target for debugging ranging. Config persists
// in EEPROM; it speaks the same newline-delimited JSON control protocol over
// USB-serial as the ESP32 nodes, so the same control-app drives it.
//
// Radio integration (SX1280 in ports/speedybee/lib, reference firmware in
// ports/speedybee/reference) is future work; today peers come from the mock
// source so a lone board still produces a real solution.
#include <Arduino.h>
#include <EEPROM.h>
#include <math.h>
#include <string.h>

extern "C" {
#include "nav/nav_core.h"
#include "nav/nav_mock.h"
#include "nav/nav_quality.h"
#include "nav/nav_serial_json.h"
#include "nav/nav_state_machine.h"
}

namespace {

constexpr uint32_t kBaud = 115200;
constexpr uint32_t kSnapshotPeriodMs = 500;
constexpr size_t kRecordBufMax = 1536;
constexpr uint8_t kDefaultNodeId = 0;
constexpr int32_t kDefaultAltitudeMm = 183500;
constexpr uint32_t kConfigMagic = 0x5342324eUL;  // "SB2N"

struct PersistConfig {
  uint32_t magic;
  uint8_t nodeId;
  char name[NAV_NODE_NAME_MAX];
  uint8_t gpsEnabled;
  uint8_t mockEnabled;
  int32_t altitudeMm;
};

PersistConfig gConfig;
nav_system_t gNav;
nav_mock_t gMock;
char gLine[256];
size_t gLineLen = 0;
uint32_t gLastSnapshotMs = 0;
bool gDebugEnabled = false;
uint32_t gLocalQualityPacketSeq = 0;

void saveConfig() {
  EEPROM.put(0, gConfig);
  EEPROM.commit();
}

void setDefaultConfig() {
  gConfig.magic = kConfigMagic;
  gConfig.nodeId = kDefaultNodeId;
  snprintf(gConfig.name, sizeof(gConfig.name), "speedybee-%u", gConfig.nodeId);
  gConfig.gpsEnabled = 0;  // no GPS hardware -> trilaterate
  gConfig.mockEnabled = 1;
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
  if (gConfig.mockEnabled > 1) {
    gConfig.mockEnabled = 1;
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
  nav_mock_set_enabled(&gMock, gConfig.mockEnabled != 0);
}

void emitIntoCore(const nav_event_t *event, void *user) {
  nav_core_handle_event(static_cast<nav_system_t *>(user), event);
}

void initCoreForConfig() {
  nav_config_t cfg = nav_config_default(gConfig.nodeId);
  cfg.min_anchor_triangle_area_m2 = 10.0f;
  cfg.degraded_anchor_triangle_area_m2 = 100.0f;
  nav_core_init(&gNav, &cfg);
  applyCoreConfig();
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
      gConfig.mockEnabled = cmd.bool_value ? 1 : 0;
      nav_mock_set_enabled(&gMock, gConfig.mockEnabled != 0);
      changed = true;
      emitLog(now, "INFO", "CONFIG", gConfig.mockEnabled != 0 ? "mock enabled" : "mock disabled");
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
      emitLog(now, "INFO", "CONFIG", gDebugEnabled ? "debug telemetry enabled" : "debug telemetry disabled");
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
  info.mock_enabled = gConfig.mockEnabled != 0;

  static char buf[kRecordBufMax];
  int written = nav_serial_write_snapshot_record(buf, sizeof(buf), &info, &snapshot, &gNav.peer_table);
  printRecord(buf, written, sizeof(buf));

  if (gDebugEnabled) {
    nav_node_quality_report_t quality;
    fillLocalNodeQualityReport(++gLocalQualityPacketSeq, snapshot, &quality);
    written = nav_serial_write_node_quality_record(buf, sizeof(buf), &quality, now, 0u, "local");
    printRecord(buf, written, sizeof(buf));
  }
}

}  // namespace

void setup() {
  Serial.begin(kBaud);
  EEPROM.begin(sizeof(PersistConfig) + 8);
  loadConfig();
  initCoreForConfig();
  emitLog(millis(), "INFO", "SYSTEM", "SpeedyBee firmware started");
}

void loop() {
  pumpSerial();
  const uint32_t now = millis();
  if (now - gLastSnapshotMs >= kSnapshotPeriodMs) {
    gLastSnapshotMs = now;
    emitRecords(now);
  }
}
