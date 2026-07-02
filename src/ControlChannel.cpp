#include "ControlChannel.h"

#include <cstdio>
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_timer.h"

#include "Logger.h"
#include "nav/nav_core.h"
#include "nav/nav_mock.h"
#include "nav/nav_quality.h"
#include "nav/nav_serial_json.h"
#include "nav/nav_state_machine.h"

#ifndef NAV_ENABLE_GPS_HEALTH
#define NAV_ENABLE_GPS_HEALTH 0
#endif

namespace {

constexpr uint32_t kSnapshotPeriodMs = 500u;
constexpr size_t kLineMax = 256u;
constexpr size_t kSnapshotBufMax = 1024u;

NodeConfig gConfig;
nav_system_t gNav;
nav_mock_t gMock;
SemaphoreHandle_t gMutex = nullptr;

void emitIntoCore(const nav_event_t *event, void *user) {
  nav_core_handle_event(static_cast<nav_system_t *>(user), event);
}

uint32_t nowMs() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

void seedMock();

bool effectiveGpsEnabled() {
#if NAV_ENABLE_GPS_HEALTH
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
  cfg.min_anchor_triangle_area_m2 = 10.0f;
  cfg.degraded_anchor_triangle_area_m2 = 100.0f;
  nav_core_init(&gNav, &cfg);
  applyCoreConfig();
  seedMock();
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
#if NAV_ENABLE_GPS_HEALTH
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
  char buf[kSnapshotBufMax];
  for (;;) {
    const uint32_t now = nowMs();
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
    nav_serial_write_snapshot(buf, sizeof(buf), &info, &snapshot, &gNav.peer_table);
    xSemaphoreGive(gMutex);

    std::printf("%s\n", buf);
    vTaskDelay(pdMS_TO_TICKS(kSnapshotPeriodMs));
  }
}

}  // namespace

namespace ControlChannel {

void begin(const NodeConfig &config) {
  gConfig = config;
#if !NAV_ENABLE_GPS_HEALTH
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

bool handleEvent(const nav_event_t *event) {
  if (event == nullptr || gMutex == nullptr) {
    return false;
  }
  xSemaphoreTake(gMutex, portMAX_DELAY);
  nav_core_handle_event(&gNav, event);
  xSemaphoreGive(gMutex);
  return true;
}

}  // namespace ControlChannel
