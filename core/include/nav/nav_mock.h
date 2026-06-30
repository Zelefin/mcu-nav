#ifndef NAV_MOCK_H
#define NAV_MOCK_H

#include "nav/nav_events.h"
#include "nav/nav_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* On-device / host mock peer source.
 *
 * Given the local node's true position and a set of peer "world" positions, the
 * mock synthesises peer telemetry beacons and range results and emits them as
 * nav_event_t values, exactly as the radio driver would. This lets a single
 * physical node exercise the full navigation core and trilateration without any
 * real peers. The range for each peer is the ECEF chord distance between the
 * local truth and the peer (nav_trilat_distance_m), so a GPS-denied local node
 * recovers its true position from the mock ranges. */

typedef struct {
    uint8_t node_id;
    nav_position_t position; /* peer true/claimed world position */
    bool gnss_valid;         /* does this peer claim a valid GNSS fix? */
    int16_t rssi_dbm;
    int16_t snr_db;
    uint32_t range_sigma_mm;
} nav_mock_peer_t;

typedef struct {
    nav_position_t local_truth; /* local node true position (range reference) */
    nav_mock_peer_t peers[NAV_MAX_NODES];
    size_t peer_count;
    bool enabled;
} nav_mock_t;

/* Callback used to deliver each synthesised event (typically wraps
 * nav_core_handle_event). */
typedef void (*nav_mock_emit_fn)(const nav_event_t *event, void *user);

void nav_mock_init(nav_mock_t *mock);
void nav_mock_set_enabled(nav_mock_t *mock, bool enabled);
void nav_mock_set_local_truth(nav_mock_t *mock, nav_position_t local_truth);

/* Adds a mock peer. Returns NAV_STATUS_NO_SPACE if the table is full,
 * NAV_STATUS_INVALID_ARGUMENT for a null mock or a reserved node id. */
nav_status_t nav_mock_add_peer(nav_mock_t *mock, const nav_mock_peer_t *peer);

/* Emits, for every mock peer, one NAV_EVT_PEER_TELEMETRY_RX followed by one
 * NAV_EVT_RANGE_RESULT stamped at now_ms. Does nothing when disabled. Returns
 * the number of events emitted. */
size_t nav_mock_emit(const nav_mock_t *mock, uint32_t now_ms, nav_mock_emit_fn emit, void *user);

#ifdef __cplusplus
}
#endif

#endif
