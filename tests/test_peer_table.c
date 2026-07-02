#include <assert.h>

#include "nav/nav_peer_table.h"

static nav_peer_telemetry_t telemetry(uint8_t node_id)
{
    nav_peer_telemetry_t t = {
        .node_id = node_id,
        .packet_seq = 42u,
        .timestamp_ms = 1000u,
        .position = {.lat_e7 = 504501000, .lon_e7 = 305234000, .alt_mm = 180000},
        .velocity = {.vel_n_mmps = 100, .vel_e_mmps = -50, .vel_d_mmps = 0},
        .fix_type = NAV_GNSS_FIX_3D,
        .gnss_valid = true,
        .satellites = 12u,
        .hdop_centi = 85u,
        .hacc_mm = 1200u,
        .vacc_mm = 1800u,
        .nav_mode = NAV_MODE_GNSS_OK,
        .solution_status = NAV_SOLUTION_GNSS_DIRECT,
        .solution_source = NAV_SOURCE_LOCAL_GNSS,
    };
    return t;
}

int main(void)
{
    nav_peer_table_t table;
    nav_peer_table_init(&table);
    assert(nav_peer_table_get(&table, 1u) == 0);

    nav_peer_telemetry_t t = telemetry(1u);
    assert(nav_peer_table_update_telemetry(&table, &t, 1100u));
    nav_peer_state_t *peer = nav_peer_table_get(&table, 1u);
    assert(peer != 0);
    assert(peer->present);
    assert(peer->packet_seq == 42u);
    assert(peer->position.alt_mm == 180000);
    assert(peer->gnss_valid);
    assert(peer->solution_status == NAV_SOLUTION_GNSS_DIRECT);
    assert(peer->solution_source == NAV_SOURCE_LOCAL_GNSS);

    nav_range_result_t range = {
        .peer_id = 1u,
        .request_id = 43u,
        .timestamp_ms = 1200u,
        .range_mm = 394135u,
        .range_sigma_mm = 150u,
        .rssi_dbm = -65,
        .snr_db = 11,
        .valid = true,
    };
    assert(nav_peer_table_update_range(&table, &range, 1200u));
    assert(peer->range_valid);
    assert(peer->last_range_request_id == 43u);
    assert(peer->range_mm == 394135u);

    nav_peer_beacon_rx_t beacon = {
        .telemetry = telemetry(2u),
        .rssi_dbm = -71,
        .snr_db = 8,
    };
    assert(nav_peer_table_update_beacon_rx(&table, &beacon, 1300u));
    peer = nav_peer_table_get(&table, 2u);
    assert(peer != 0);
    assert(peer->packet_seq == 42u);
    assert(peer->rssi_dbm == -71);
    assert(peer->snr_db == 8);

    nav_peer_table_mark_stale(&table, 2501u, 1000u, 1000u);
    assert(!peer->gnss_valid);
    assert(!peer->range_valid);
    assert(peer->last_reject_reason == NAV_REJECT_STALE_TELEMETRY);
    return 0;
}
