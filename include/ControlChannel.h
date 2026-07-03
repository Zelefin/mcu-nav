#ifndef CONTROL_CHANNEL_H
#define CONTROL_CHANNEL_H

#include "NodeConfig.h"

#include "nav/nav_events.h"

// USB-serial control channel to the browser telemetry UI. Speaks newline
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

// Runtime-only debug telemetry flag. It is intentionally separate from
// NodeConfig/NVS so debug mode always boots off.
bool isDebugEnabled();

// Returns whether local GNSS is allowed to drive this node's solution. When
// false, the UART may still be read for diagnostics but this node is
// GPS-denied and does not advertise itself as a GNSS anchor.
bool isGpsEnabled();

// Builds this node's mappable telemetry beacon for over-the-air display and
// anchor discovery. GNSS beacons set gnss_valid for anchor eligibility; accepted
// RADIO_3D beacons carry source metadata with gnss_valid=false for display only.
bool getLocalTelemetry(uint32_t packetSeq, nav_peer_telemetry_t *out);

// Builds this node's latest diagnostics-only quality report for OTA debug
// telemetry. Safe to call from the radio task; does not mutate solver state.
bool getLocalNodeQualityReport(uint32_t packetSeq, nav_node_quality_report_t *out);

// Buffers a peer quality report received over the air. Diagnostics only; never
// feeds the navigation core or anchor table.
bool handleNodeQualityReport(const nav_node_quality_report_t *report, uint32_t receivedMs);

// Emits a typed range record on the control channel. Used for local range
// events and decoded best-effort air reports.
bool emitRangeRecord(const nav_serial_range_record_t *record);

// Injects a platform event into the owned navigation core. Safe to call from
// other ESP tasks after begin().
bool handleEvent(const nav_event_t *event);
}  // namespace ControlChannel

#endif
