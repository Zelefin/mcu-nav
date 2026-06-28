#include <stdio.h>

#include "nav/nav_core.h"
#include "nav/nav_quality.h"
#include "nav/nav_state_machine.h"

static nav_peer_telemetry_t peer_telemetry(uint8_t node_id)
{
    static const nav_position_t positions[NAV_MAX_NODES] = {
        {0},
        {.lat_e7 = 504501000, .lon_e7 = 305234000, .alt_mm = 180000},
        {.lat_e7 = 504565000, .lon_e7 = 305201000, .alt_mm = 190000},
        {.lat_e7 = 504510000, .lon_e7 = 305340000, .alt_mm = 175000},
    };
    nav_peer_telemetry_t telemetry = {
        .node_id = node_id,
        .packet_seq = (uint32_t)(100u + node_id),
        .timestamp_ms = 1000u,
        .position = positions[node_id],
        .velocity = {0},
        .fix_type = NAV_GNSS_FIX_3D,
        .gnss_valid = true,
        .satellites = 12u,
        .hdop_centi = 80u,
        .hacc_mm = 1000u,
        .vacc_mm = 1500u,
        .nav_mode = NAV_MODE_GNSS_OK,
    };
    return telemetry;
}

static nav_range_result_t peer_range(uint8_t node_id)
{
    static const uint32_t ranges_mm[NAV_MAX_NODES] = {
        0u,
        394135u,
        621957u,
        553391u,
    };
    nav_range_result_t range = {
        .peer_id = node_id,
        .request_id = (uint16_t)(200u + node_id),
        .timestamp_ms = 1000u,
        .range_mm = ranges_mm[node_id],
        .range_sigma_mm = 100u,
        .rssi_dbm = -60,
        .snr_db = 10,
        .valid = true,
    };
    return range;
}

static void posix_log(
    uint32_t timestamp_ms,
    nav_log_level_t level,
    nav_log_category_t category,
    const char *event,
    const char *message,
    void *user
)
{
    (void)user;
    printf(
        "t=%lu level=%s cat=%s event=%s %s\n",
        (unsigned long)timestamp_ms,
        nav_log_level_to_string(level),
        nav_log_category_to_string(category),
        event,
        message
    );
}

int main(void)
{
    nav_config_t config = nav_config_default(0u);
    config.demo_force_gps_denied = true;
    config.min_anchor_triangle_area_m2 = 10.0f;
    config.degraded_anchor_triangle_area_m2 = 100.0f;

    nav_system_t nav;
    nav_core_init(&nav, &config);
    nav_logger_t logger;
    nav_logger_init(&logger, posix_log, NULL, NAV_LOG_INFO);
    nav_core_set_logger(&nav, &logger);

    nav_event_t event = {
        .type = NAV_EVT_LOCAL_ALTITUDE_SAMPLE,
        .timestamp_ms = 1000u,
        .data.local_altitude = {
            .alt_mm = 183500,
            .timestamp_ms = 1000u,
            .source = NAV_ALT_SOURCE_SIM,
            .valid = true,
        },
    };
    nav_core_handle_event(&nav, &event);

    for (uint8_t peer = 1u; peer <= 3u; ++peer) {
        event.type = NAV_EVT_PEER_TELEMETRY_RX;
        event.timestamp_ms = 1000u;
        event.data.peer_beacon_rx = (nav_peer_beacon_rx_t){
            .telemetry = peer_telemetry(peer),
            .rssi_dbm = -60,
            .snr_db = 10,
        };
        nav_core_handle_event(&nav, &event);

        event.type = NAV_EVT_RANGE_RESULT;
        event.data.range_result = peer_range(peer);
        nav_core_handle_event(&nav, &event);
    }

    nav_core_tick(&nav, 1000u);

    nav_snapshot_t snapshot;
    (void)nav_core_get_snapshot(&nav, &snapshot);
    printf("nav_posix_demo node=%u\n", snapshot.node_id);
    printf("mode=%s\n", nav_mode_to_string(snapshot.nav_mode));
    printf("solution=%s\n", nav_solution_status_to_string(snapshot.solution_status));
    printf("source=%s\n", nav_solution_source_to_string(snapshot.solution_source));
    printf("lat_e7=%ld\n", (long)snapshot.position.lat_e7);
    printf("lon_e7=%ld\n", (long)snapshot.position.lon_e7);
    printf("alt_mm=%ld\n", (long)snapshot.position.alt_mm);
    printf(
        "anchors=%u,%u,%u\n",
        snapshot.selected_anchor_node_ids[0],
        snapshot.selected_anchor_node_ids[1],
        snapshot.selected_anchor_node_ids[2]
    );
    printf(
        "residuals_mm=%ld,%ld,%ld\n",
        (long)snapshot.anchor_residuals_mm[0],
        (long)snapshot.anchor_residuals_mm[1],
        (long)snapshot.anchor_residuals_mm[2]
    );
    printf("residual_rms_m=%.6f\n", (double)snapshot.residual_rms_m);
    printf("quality=%.3f\n", (double)snapshot.total_quality);
    printf("reject_reason=%s\n", nav_reject_reason_to_string(snapshot.reject_reason));
    return 0;
}
