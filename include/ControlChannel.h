#ifndef CONTROL_CHANNEL_H
#define CONTROL_CHANNEL_H

#include "NodeConfig.h"

// USB-serial control channel to the browser control-app. Speaks newline
// delimited JSON: it streams a navigation snapshot line periodically and applies
// inbound command lines (rename node, GPS on/off, mock on/off, set altitude),
// persisting changes to NVS. Runs the navigation core and, when mock is enabled,
// the synthetic peer source so a single board produces a real solution.
namespace ControlChannel {
// Initialises the navigation core and mock from persisted config, then starts
// the reader and snapshot-emitter tasks.
void begin(const NodeConfig &config);
}  // namespace ControlChannel

#endif
