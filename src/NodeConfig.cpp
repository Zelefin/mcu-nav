#include "NodeConfig.h"

#include <cstring>

#include "nvs.h"
#include "nvs_flash.h"

#include "Logger.h"

namespace {
constexpr char kNamespace[] = "navcfg";
constexpr char kKeyName[] = "name";
constexpr char kKeyGps[] = "gps";
constexpr char kKeyMock[] = "mock";
constexpr char kKeyAlt[] = "alt_mm";
constexpr char kKeyNodeId[] = "node_id";

bool ensureNvsReady() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    err = nvs_flash_init();
  }
  return err == ESP_OK;
}
}  // namespace

namespace NodeConfigStore {

NodeConfig load(uint8_t defaultNodeId) {
  NodeConfig config;
  config.nodeId = defaultNodeId;
  std::snprintf(config.name, sizeof(config.name), "node-%u", static_cast<unsigned>(defaultNodeId));
  config.gpsEnabled = true;
  config.mockEnabled = false;
  config.altitudeMm = 0;

  if (!ensureNvsReady()) {
    Logger::warnf("CONFIG", "NVS unavailable, using defaults");
    return config;
  }

  nvs_handle_t handle;
  if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) {
    // First boot: no namespace yet. Persist defaults so they are stable.
    save(config);
    return config;
  }

  size_t nameLen = sizeof(config.name);
  nvs_get_str(handle, kKeyName, config.name, &nameLen);
  uint8_t u8 = 0;
  if (nvs_get_u8(handle, kKeyNodeId, &u8) == ESP_OK) config.nodeId = u8;
  if (nvs_get_u8(handle, kKeyGps, &u8) == ESP_OK) config.gpsEnabled = u8 != 0;
  if (nvs_get_u8(handle, kKeyMock, &u8) == ESP_OK) config.mockEnabled = u8 != 0;
  int32_t i32 = 0;
  if (nvs_get_i32(handle, kKeyAlt, &i32) == ESP_OK) config.altitudeMm = i32;
  nvs_close(handle);
  return config;
}

bool save(const NodeConfig &config) {
  if (!ensureNvsReady()) {
    return false;
  }
  nvs_handle_t handle;
  if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) {
    return false;
  }
  bool ok = true;
  ok = ok && nvs_set_str(handle, kKeyName, config.name) == ESP_OK;
  ok = ok && nvs_set_u8(handle, kKeyNodeId, config.nodeId) == ESP_OK;
  ok = ok && nvs_set_u8(handle, kKeyGps, config.gpsEnabled ? 1 : 0) == ESP_OK;
  ok = ok && nvs_set_u8(handle, kKeyMock, config.mockEnabled ? 1 : 0) == ESP_OK;
  ok = ok && nvs_set_i32(handle, kKeyAlt, config.altitudeMm) == ESP_OK;
  ok = ok && nvs_commit(handle) == ESP_OK;
  nvs_close(handle);
  return ok;
}

}  // namespace NodeConfigStore
