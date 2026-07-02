#include <assert.h>
#include <stdlib.h>

#include "nav/nav_core.h"
#include "nav/nav_mock.h"

static void emit_into_core(const nav_event_t *event, void *user)
{
    nav_core_handle_event((nav_system_t *)user, event);
}

static int32_t abs_i32(int32_t v)
{
    return v < 0 ? -v : v;
}

int main(void)
{
    /* Disabled mock emits nothing. */
    nav_mock_t mock;
    nav_mock_init(&mock);
    size_t emitted = nav_mock_emit(&mock, 1000u, emit_into_core, NULL);
    assert(emitted == 0u);

    /* Local node truth and three GPS-good mock peers around it. */
    const nav_position_t local_truth = {.lat_e7 = 504520000, .lon_e7 = 305260000, .alt_mm = 183500};
    nav_mock_set_local_truth(&mock, local_truth);
    nav_mock_set_enabled(&mock, true);

    const nav_mock_peer_t peers[3] = {
        {.node_id = 1u, .position = {504501000, 305234000, 180000}, .gnss_valid = true, .rssi_dbm = -60, .snr_db = 10, .range_sigma_mm = 100u},
        {.node_id = 2u, .position = {504565000, 305201000, 190000}, .gnss_valid = true, .rssi_dbm = -62, .snr_db = 9, .range_sigma_mm = 100u},
        {.node_id = 3u, .position = {504510000, 305340000, 175000}, .gnss_valid = true, .rssi_dbm = -64, .snr_db = 8, .range_sigma_mm = 100u},
    };
    for (size_t i = 0; i < 3; ++i) {
        assert(nav_mock_add_peer(&mock, &peers[i]) == NAV_STATUS_OK);
    }

    /* Local node is GPS-denied, so it must trilaterate from the mock ranges. */
    nav_config_t config = nav_config_default(0u);
    config.demo_force_gps_denied = true;
    config.min_anchor_triangle_area_m2 = 10.0f;
    config.degraded_anchor_triangle_area_m2 = 100.0f;

    nav_system_t nav;
    nav_core_init(&nav, &config);

    nav_event_t altitude = {
        .type = NAV_EVT_LOCAL_ALTITUDE_SAMPLE,
        .timestamp_ms = 1000u,
        .data.local_altitude = {
            .alt_mm = local_truth.alt_mm,
            .timestamp_ms = 1000u,
            .source = NAV_ALT_SOURCE_MANUAL,
            .valid = true,
        },
    };
    nav_core_handle_event(&nav, &altitude);

    emitted = nav_mock_emit(&mock, 1000u, emit_into_core, &nav);
    assert(emitted == 6u); /* telemetry + range per peer */

    nav_core_tick(&nav, 1000u);

    nav_snapshot_t snapshot;
    assert(nav_core_get_snapshot(&nav, &snapshot));
    assert(snapshot.solution_status == NAV_SOLUTION_RADIO_3D);
    assert(snapshot.solution_source == NAV_SOURCE_RADIO_3D);
    assert(snapshot.num_anchors == 3u);

    /* The recovered position must match the local truth to within ~1 m. */
    assert(abs_i32(snapshot.position.lat_e7 - local_truth.lat_e7) < 100);
    assert(abs_i32(snapshot.position.lon_e7 - local_truth.lon_e7) < 100);

    return 0;
}
