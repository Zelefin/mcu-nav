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
constexpr uint8_t kDefaultNodeId = 5;
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

void loadConfig() {
  EEPROM.get(0, gConfig);
  if (gConfig.magic != kConfigMagic) {
    gConfig.magic = kConfigMagic;
    gConfig.nodeId = kDefaultNodeId;
    snprintf(gConfig.name, sizeof(gConfig.name), "speedybee-%u", gConfig.nodeId);
    gConfig.gpsEnabled = 0;  // no GPS hardware -> trilaterate
    gConfig.mockEnabled = 1;
    gConfig.altitudeMm = 0;
    EEPROM.put(0, gConfig);
    EEPROM.commit();
  }
}

void saveConfig() {
  EEPROM.put(0, gConfig);
  EEPROM.commit();
}

void applyCoreConfig() { gNav.config.demo_force_gps_denied = gConfig.gpsEnabled == 0; }

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

void applyCommand(const nav_ctrl_command_t &cmd) {
  bool changed = false;
  switch (cmd.type) {
    case NAV_CTRL_CMD_SET_NAME:
      strncpy(gConfig.name, cmd.str_value, sizeof(gConfig.name) - 1);
      gConfig.name[sizeof(gConfig.name) - 1] = '\0';
      changed = true;
      break;
    case NAV_CTRL_CMD_SET_GPS:
      gConfig.gpsEnabled = cmd.bool_value ? 1 : 0;
      applyCoreConfig();
      changed = true;
      break;
    case NAV_CTRL_CMD_SET_MOCK:
      gConfig.mockEnabled = cmd.bool_value ? 1 : 0;
      nav_mock_set_enabled(&gMock, gConfig.mockEnabled != 0);
      changed = true;
      break;
    case NAV_CTRL_CMD_SET_ALTITUDE:
      gConfig.altitudeMm = cmd.int_value;
      changed = true;
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

void emitSnapshot(uint32_t now) {
  nav_event_t altitude = {};
  altitude.type = NAV_EVT_LOCAL_ALTITUDE_SAMPLE;
  altitude.timestamp_ms = now;
  altitude.data.local_altitude.alt_mm = gConfig.altitudeMm;
  altitude.data.local_altitude.timestamp_ms = now;
  altitude.data.local_altitude.source = NAV_ALT_SOURCE_MANUAL;
  altitude.data.local_altitude.valid = true;
  nav_core_handle_event(&gNav, &altitude);

  nav_mock_emit(&gMock, now, emitIntoCore, &gNav);
  nav_core_tick(&gNav, now);

  nav_snapshot_t snapshot;
  nav_core_get_snapshot(&gNav, &snapshot);
  nav_serial_node_info_t info;
  info.node_id = gConfig.nodeId;
  info.node_name = gConfig.name;
  info.gps_enabled = gConfig.gpsEnabled != 0;
  info.mock_enabled = gConfig.mockEnabled != 0;

  static char buf[1024];
  nav_serial_write_snapshot(buf, sizeof(buf), &info, &snapshot, &gNav.peer_table);
  Serial.println(buf);
}

}  // namespace

void setup() {
  Serial.begin(kBaud);
  EEPROM.begin(sizeof(PersistConfig) + 8);
  loadConfig();

  nav_config_t cfg = nav_config_default(gConfig.nodeId);
  cfg.min_anchor_triangle_area_m2 = 10.0f;
  cfg.degraded_anchor_triangle_area_m2 = 100.0f;
  nav_core_init(&gNav, &cfg);
  applyCoreConfig();
  seedMock();
}

void loop() {
  pumpSerial();
  const uint32_t now = millis();
  if (now - gLastSnapshotMs >= kSnapshotPeriodMs) {
    gLastSnapshotMs = now;
    emitSnapshot(now);
  }
}
