#include <stdio.h>

#include "nav/nav_core.h"
#include "nav/nav_mock.h"
#include "nav/nav_quality.h"
#include "nav/nav_serial_json.h"
#include "nav/nav_state_machine.h"

/* Host demo of the "single physical node" development flow: a GPS-denied local
 * node recovers its position by trilaterating against mock peers, then prints the
 * exact JSON snapshot a real node would stream to the browser telemetry UI over
 * USB-serial. No radio, no peers, no four-node setup required. */

static void emit_into_core(const nav_event_t *event, void *user)
{
    nav_core_handle_event((nav_system_t *)user, event);
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
    const nav_position_t local_truth = {.lat_e7 = 504520000, .lon_e7 = 305260000, .alt_mm = 183500};

    nav_mock_t mock;
    nav_mock_init(&mock);
    nav_mock_set_local_truth(&mock, local_truth);
    nav_mock_set_enabled(&mock, true);
    const nav_mock_peer_t peers[3] = {
        {.node_id = 1u, .position = {504501000, 305234000, 180000}, .gnss_valid = true, .rssi_dbm = -60, .snr_db = 10, .range_sigma_mm = 100u},
        {.node_id = 2u, .position = {504565000, 305201000, 190000}, .gnss_valid = true, .rssi_dbm = -62, .snr_db = 9, .range_sigma_mm = 100u},
        {.node_id = 3u, .position = {504510000, 305340000, 175000}, .gnss_valid = true, .rssi_dbm = -64, .snr_db = 8, .range_sigma_mm = 100u},
    };
    for (size_t i = 0; i < 3; ++i) {
        (void)nav_mock_add_peer(&mock, &peers[i]);
    }

    nav_config_t config = nav_config_default(0u);
    config.demo_force_gps_denied = true;
    config.min_anchor_triangle_area_m2 = 10.0f;
    config.degraded_anchor_triangle_area_m2 = 100.0f;

    nav_system_t nav;
    nav_core_init(&nav, &config);
    nav_logger_t logger;
    nav_logger_init(&logger, posix_log, NULL, NAV_LOG_INFO);
    nav_core_set_logger(&nav, &logger);

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

    (void)nav_mock_emit(&mock, 1000u, emit_into_core, &nav);
    nav_core_tick(&nav, 1000u);

    nav_snapshot_t snapshot;
    (void)nav_core_get_snapshot(&nav, &snapshot);

    nav_serial_node_info_t info = {
        .node_id = config.local_node_id,
        .node_name = "posix-demo",
        .gps_enabled = !config.demo_force_gps_denied,
        .mock_enabled = mock.enabled,
    };
    char line[1024];
    (void)nav_serial_write_snapshot(line, sizeof(line), &info, &snapshot, &nav.peer_table);

    printf("nav_posix_demo node=%u\n", snapshot.node_id);
    printf("mode=%s solution=%s source=%s\n",
           nav_mode_to_string(snapshot.nav_mode),
           nav_solution_status_to_string(snapshot.solution_status),
           nav_solution_source_to_string(snapshot.solution_source));
    printf("recovered lat_e7=%ld lon_e7=%ld alt_mm=%ld (truth %ld,%ld,%ld)\n",
           (long)snapshot.position.lat_e7, (long)snapshot.position.lon_e7, (long)snapshot.position.alt_mm,
           (long)local_truth.lat_e7, (long)local_truth.lon_e7, (long)local_truth.alt_mm);
    printf("telemetry-ui line: %s\n", line);
    return 0;
}
