#include "nav/nav_mock.h"

#include <string.h>

#include "nav/nav_trilateration.h"

#define NAV_E7_PER_DEG 1.0e7
#define NAV_MM_PER_M 1000.0

void nav_mock_init(nav_mock_t *mock)
{
    if (mock == NULL) {
        return;
    }
    memset(mock, 0, sizeof(*mock));
}

void nav_mock_set_enabled(nav_mock_t *mock, bool enabled)
{
    if (mock == NULL) {
        return;
    }
    mock->enabled = enabled;
}

void nav_mock_set_local_truth(nav_mock_t *mock, nav_position_t local_truth)
{
    if (mock == NULL) {
        return;
    }
    mock->local_truth = local_truth;
}

nav_status_t nav_mock_add_peer(nav_mock_t *mock, const nav_mock_peer_t *peer)
{
    if (mock == NULL || peer == NULL || peer->node_id == NAV_INVALID_NODE_ID) {
        return NAV_STATUS_INVALID_ARGUMENT;
    }
    if (mock->peer_count >= NAV_MAX_NODES) {
        return NAV_STATUS_NO_SPACE;
    }
    mock->peers[mock->peer_count] = *peer;
    mock->peer_count++;
    return NAV_STATUS_OK;
}

static uint32_t mock_range_mm(nav_position_t local, nav_position_t peer)
{
    const double meters = nav_trilat_distance_m(
        (double)local.lat_e7 / NAV_E7_PER_DEG,
        (double)local.lon_e7 / NAV_E7_PER_DEG,
        (double)local.alt_mm / NAV_MM_PER_M,
        (double)peer.lat_e7 / NAV_E7_PER_DEG,
        (double)peer.lon_e7 / NAV_E7_PER_DEG,
        (double)peer.alt_mm / NAV_MM_PER_M
    );
    if (meters <= 0.0) {
        return 0u;
    }
    return (uint32_t)(meters * NAV_MM_PER_M + 0.5);
}

size_t nav_mock_emit(const nav_mock_t *mock, uint32_t now_ms, nav_mock_emit_fn emit, void *user)
{
    if (mock == NULL || emit == NULL || !mock->enabled) {
        return 0u;
    }

    size_t emitted = 0u;
    for (size_t i = 0; i < mock->peer_count; ++i) {
        const nav_mock_peer_t *peer = &mock->peers[i];

        nav_event_t telemetry_event = {
            .type = NAV_EVT_PEER_TELEMETRY_RX,
            .timestamp_ms = now_ms,
            .data.peer_beacon_rx = {
                .telemetry = {
                    .node_id = peer->node_id,
                    .packet_seq = now_ms,
                    .timestamp_ms = now_ms,
                    .position = peer->position,
                    .velocity = {0},
                    .fix_type = peer->gnss_valid ? NAV_GNSS_FIX_3D : NAV_GNSS_FIX_NONE,
                    .gnss_valid = peer->gnss_valid,
                    .satellites = peer->gnss_valid ? 12u : 0u,
                    .hdop_centi = peer->gnss_valid ? 80u : 0u,
                    .hacc_mm = peer->gnss_valid ? 1000u : 0u,
                    .vacc_mm = peer->gnss_valid ? 1500u : 0u,
                    .nav_mode = peer->gnss_valid ? NAV_MODE_GNSS_OK : NAV_MODE_NO_NAV_SOLUTION,
                },
                .rssi_dbm = peer->rssi_dbm,
                .snr_db = peer->snr_db,
            },
        };
        emit(&telemetry_event, user);
        emitted++;

        nav_event_t range_event = {
            .type = NAV_EVT_RANGE_RESULT,
            .timestamp_ms = now_ms,
            .data.range_result = {
                .peer_id = peer->node_id,
                .request_id = (uint16_t)(now_ms + peer->node_id),
                .timestamp_ms = now_ms,
                .range_mm = mock_range_mm(mock->local_truth, peer->position),
                .range_sigma_mm = peer->range_sigma_mm,
                .rssi_dbm = peer->rssi_dbm,
                .snr_db = peer->snr_db,
                .valid = true,
            },
        };
        emit(&range_event, user);
        emitted++;
    }
    return emitted;
}
