#ifndef NODE_CONFIG_H
#define NODE_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#include "nav/nav_serial_json.h"

// Persistent per-node settings, stored in NVS so a node keeps its name and its
// GPS/mock/altitude choices across reboots. The telemetry UI edits these over the
// USB-serial control channel.
struct NodeConfig {
  uint8_t nodeId;
  char name[NAV_NODE_NAME_MAX];
  bool gpsEnabled;   // false => node trilaterates instead of using local GNSS
  bool mockEnabled;  // inject synthetic peers for single-node testing
  int32_t altitudeMm;  // constant altitude used when GNSS altitude is unavailable
};

namespace NodeConfigStore {
// Loads config from NVS, filling defaults (and persisting them) on first boot.
NodeConfig load(uint8_t defaultNodeId);

// Persists the whole config. Returns true on success.
bool save(const NodeConfig &config);
}  // namespace NodeConfigStore

#endif
