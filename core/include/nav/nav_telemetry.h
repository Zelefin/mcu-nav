#ifndef NAV_TELEMETRY_H
#define NAV_TELEMETRY_H

#include <stddef.h>

#include "nav/nav_events.h"
#include "nav/nav_radio_protocol.h"
#include "nav/nav_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Over-the-air telemetry codec.
 *
 * nav_radio_protocol owns the frame envelope (magic, type, seq, length); this
 * module owns the little-endian (de)serialisation of the typed payload bodies
 * documented in docs/radio_protocol.md and the mapping of a received frame onto
 * a nav_event_t. A node uses it to transmit its own beacon / range results and
 * to turn bytes received from the SX1280 into core events. Pure and
 * host-testable; no radio driver dependency. */

/* Encodes a node's own telemetry as a complete BEACON_RX frame. */
nav_status_t nav_telemetry_encode_beacon(
    const nav_peer_telemetry_t *telemetry,
    uint16_t packet_seq,
    uint8_t *out,
    size_t out_capacity,
    size_t *out_len
);

/* Encodes a successful range result as a complete RANGE_RESULT frame. */
nav_status_t nav_telemetry_encode_range_result(
    const nav_range_result_t *range,
    uint16_t frame_seq,
    uint8_t *out,
    size_t out_capacity,
    size_t *out_len
);

/* Encodes a runtime-only DEBUG_ENABLE broadcast as a complete frame. */
nav_status_t nav_telemetry_encode_debug_enable(
    const nav_debug_enable_t *debug,
    uint16_t frame_seq,
    uint8_t *out,
    size_t out_capacity,
    size_t *out_len
);

/* Encodes liveness and TDMA timing as a complete HEARTBEAT frame. */
nav_status_t nav_telemetry_encode_heartbeat(
    const nav_radio_heartbeat_payload_t *heartbeat,
    uint16_t frame_seq,
    uint8_t *out,
    size_t out_capacity,
    size_t *out_len
);

/* Decodes a HEARTBEAT payload from an already decoded radio frame. */
nav_status_t nav_telemetry_decode_heartbeat(
    const nav_radio_frame_t *frame,
    nav_radio_heartbeat_payload_t *out
);

/* Decodes a DEBUG_ENABLE payload from an already decoded radio frame. */
nav_status_t nav_telemetry_decode_debug_enable(const nav_radio_frame_t *frame, nav_debug_enable_t *out);

/* Encodes a compact diagnostics-only node quality report as a complete frame. */
nav_status_t nav_telemetry_encode_node_quality_report(
    const nav_node_quality_report_t *report,
    uint16_t frame_seq,
    uint8_t *out,
    size_t out_capacity,
    size_t *out_len
);

/* Decodes a diagnostics-only node quality report from an already decoded frame. */
nav_status_t nav_telemetry_decode_node_quality_report(
    const nav_radio_frame_t *frame,
    nav_node_quality_report_t *out
);

/* Decodes one received radio frame into a nav_event_t. rssi_dbm/snr_db are the
 * local receive metadata stamped onto beacon events; now_ms timestamps the
 * event. Returns NAV_STATUS_NOT_IMPLEMENTED for a well-formed frame whose type
 * does not map to a core event, or NAV_STATUS_BAD_FRAME for undecodable bytes. */
nav_status_t nav_telemetry_decode_event(
    const uint8_t *bytes,
    size_t len,
    uint32_t now_ms,
    int16_t rssi_dbm,
    int16_t snr_db,
    nav_event_t *out_event
);

#ifdef __cplusplus
}
#endif

#endif
