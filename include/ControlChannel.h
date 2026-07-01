#ifndef CONTROL_CHANNEL_H
#define CONTROL_CHANNEL_H

#include "NodeConfig.h"

#include "nav/nav_events.h"

// USB-serial control channel to the browser control-app. Speaks newline
// delimited JSON: it streams a navigation snapshot line periodically and applies
// inbound command lines (rename node, GPS on/off, mock on/off, set altitude),
// persisting changes to NVS. Runs the navigation core and, when mock is enabled,
// the synthetic peer source so a single board produces a real solution.
namespace ControlChannel {
// Initialises the navigation core and mock from persisted config, then starts
// the reader and snapshot-emitter tasks.
void begin(const NodeConfig &config);

// Returns the latest persisted/runtime config snapshot. Safe to call from other
// ESP tasks after begin().
bool getConfig(NodeConfig *out);

// Injects a platform event into the owned navigation core. Safe to call from
// other ESP tasks after begin().
bool handleEvent(const nav_event_t *event);
}  // namespace ControlChannel

#endif
