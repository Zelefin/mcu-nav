#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nav/nav_core.h"
#include "nav/nav_quality.h"
#include "nav/nav_state_machine.h"

#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            fprintf(stderr, "[radio_navigation] CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #condition);        \
            abort();                                                                                                   \
        }                                                                                                              \
    } while (0)

#define RUN_TEST(fn)                                                                                                   \
    do {                                                                                                               \
        printf("[radio_navigation] %s\n", #fn);                                                                        \
        fn();                                                                                                          \
    } while (0)

typedef struct {
    char messages[64][192];
    size_t count;
} log_capture_t;

static void capture_log(
    uint32_t timestamp_ms,
    nav_log_level_t level,
    nav_log_category_t category,
    const char *event,
    const char *message,
    void *user
)
{
    (void)timestamp_ms;
    (void)level;
    (void)category;
    log_capture_t *capture = (log_capture_t *)user;
    if (capture->count >= 64u) {
        return;
    }
    (void)snprintf(capture->messages[capture->count], sizeof(capture->messages[capture->count]), "%s %s", event, message);
    ++capture->count;
}

static bool logs_contain(const log_capture_t *capture, const char *needle)
{
    for (size_t i = 0u; i < capture->count; ++i) {
        if (strstr(capture->messages[i], needle) != 0) {
            return true;
        }
    }
    return false;
}

static nav_peer_telemetry_t sample_telemetry(uint8_t node_id)
{
    static const nav_position_t positions[4] = {
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

static nav_range_result_t sample_range(uint8_t node_id)
{
    static const uint32_t ranges_mm[4] = {
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

static nav_event_t altitude_event(uint32_t now_ms)
{
    nav_event_t event = {
        .type = NAV_EVT_LOCAL_ALTITUDE_SAMPLE,
        .timestamp_ms = now_ms,
        .data.local_altitude = {
            .alt_mm = 183500,
            .timestamp_ms = now_ms,
            .source = NAV_ALT_SOURCE_SIM,
            .valid = true,
        },
    };
    return event;
}

static nav_event_t local_gnss_event(uint32_t now_ms)
{
    nav_event_t event = {
        .type = NAV_EVT_LOCAL_GNSS_SAMPLE,
        .timestamp_ms = now_ms,
        .data.local_gnss = {
            .node_id = 0u,
            .timestamp_ms = now_ms,
            .position = {.lat_e7 = 111111111, .lon_e7 = 222222222, .alt_mm = 999000},
            .fix_type = NAV_GNSS_FIX_3D,
            .valid = true,
            .satellites = 14u,
            .hdop_centi = 70u,
            .hacc_mm = 900u,
            .vacc_mm = 1200u,
        },
    };
    return event;
}

static void init_system(nav_system_t *sys, log_capture_t *capture)
{
    nav_config_t config = nav_config_default(0u);
    config.demo_force_gps_denied = true;
    config.min_anchor_triangle_area_m2 = 10.0f;
    config.degraded_anchor_triangle_area_m2 = 100.0f;
    nav_core_init(sys, &config);
    nav_logger_t logger;
    nav_logger_init(&logger, capture_log, capture, NAV_LOG_TRACE);
    nav_core_set_logger(sys, &logger);
}

static void send_altitude(nav_system_t *sys)
{
    nav_event_t event = altitude_event(1000u);
    nav_core_handle_event(sys, &event);
}

static void send_peer(nav_system_t *sys, uint8_t node_id, bool telemetry_valid, bool range_valid)
{
    nav_event_t event = {
        .type = NAV_EVT_PEER_TELEMETRY_RX,
        .timestamp_ms = telemetry_valid ? 1000u : 0u,
        .data.peer_beacon_rx = {
            .telemetry = {0},
            .rssi_dbm = -70,
            .snr_db = 7,
        },
    };
    event.data.peer_beacon_rx.telemetry = sample_telemetry(node_id);
    if (!telemetry_valid) {
        event.data.peer_beacon_rx.telemetry.timestamp_ms = 0u;
    }
    nav_core_handle_event(sys, &event);

    event.type = NAV_EVT_RANGE_RESULT;
    event.timestamp_ms = range_valid ? 1000u : 0u;
    event.data.range_result = sample_range(node_id);
    if (!range_valid) {
        event.data.range_result.timestamp_ms = 0u;
    }
    nav_core_handle_event(sys, &event);
}

static nav_snapshot_t tick_and_snapshot(nav_system_t *sys, uint32_t now_ms)
{
    nav_core_tick(sys, now_ms);
    nav_snapshot_t snapshot;
    CHECK(nav_core_get_snapshot(sys, &snapshot));
    return snapshot;
}

static void test_radio_3d_solution_exact(void)
{
    nav_system_t sys;
    log_capture_t logs = {0};
    init_system(&sys, &logs);
    send_altitude(&sys);
    send_peer(&sys, 1u, true, true);
    send_peer(&sys, 2u, true, true);
    send_peer(&sys, 3u, true, true);

    nav_snapshot_t snapshot = tick_and_snapshot(&sys, 1000u);
    CHECK(snapshot.solution_status == NAV_SOLUTION_RADIO_3D);
    CHECK(snapshot.solution_source == NAV_SOURCE_RADIO_3D);
    CHECK(snapshot.nav_mode == NAV_MODE_RADIO_NAV_OK);
    CHECK(snapshot.num_anchors == 3u);
    CHECK(snapshot.selected_anchor_node_ids[0] != NAV_INVALID_NODE_ID);
    CHECK(labs(snapshot.position.lat_e7 - 504529000) <= 2);
    CHECK(labs(snapshot.position.lon_e7 - 305268000) <= 2);
    CHECK(snapshot.position.alt_mm == 183500);
    CHECK(snapshot.residual_rms_m < 0.002f);
    CHECK(snapshot.max_residual_m < 0.002f);
    CHECK(logs_contain(&logs, "solve_succeeded"));
}

static void test_radio_3d_reject_not_enough_anchors(void)
{
    nav_system_t sys;
    log_capture_t logs = {0};
    init_system(&sys, &logs);
    send_altitude(&sys);
    send_peer(&sys, 1u, true, true);
    send_peer(&sys, 2u, true, true);

    nav_snapshot_t snapshot = tick_and_snapshot(&sys, 1000u);
    CHECK(snapshot.solution_status == NAV_SOLUTION_REJECTED);
    CHECK(snapshot.solution_source == NAV_SOURCE_NONE);
    CHECK(snapshot.reject_reason == NAV_REJECT_NOT_ENOUGH_ANCHORS);
}

static void test_radio_3d_reject_stale_telemetry(void)
{
    nav_system_t sys;
    log_capture_t logs = {0};
    init_system(&sys, &logs);
    send_altitude(&sys);
    send_peer(&sys, 1u, true, true);
    send_peer(&sys, 2u, true, true);
    send_peer(&sys, 3u, false, true);

    nav_snapshot_t snapshot = tick_and_snapshot(&sys, 3001u);
    CHECK(snapshot.solution_status == NAV_SOLUTION_REJECTED);
    CHECK(snapshot.reject_reason == NAV_REJECT_NOT_ENOUGH_ANCHORS);
    CHECK(snapshot.rejected_count > 0u);
    CHECK(logs_contain(&logs, "reason=STALE_TELEMETRY"));
}

static void test_radio_3d_reject_stale_range(void)
{
    nav_system_t sys;
    log_capture_t logs = {0};
    init_system(&sys, &logs);
    send_altitude(&sys);
    send_peer(&sys, 1u, true, true);
    send_peer(&sys, 2u, true, true);
    send_peer(&sys, 3u, true, false);

    nav_snapshot_t snapshot = tick_and_snapshot(&sys, 2001u);
    CHECK(snapshot.solution_status == NAV_SOLUTION_REJECTED);
    CHECK(snapshot.reject_reason == NAV_REJECT_NOT_ENOUGH_ANCHORS);
    CHECK(logs_contain(&logs, "reason=STALE_RANGE"));
}

static void test_radio_3d_reject_bad_peer_gnss(void)
{
    nav_system_t sys;
    log_capture_t logs = {0};
    init_system(&sys, &logs);
    send_altitude(&sys);
    send_peer(&sys, 1u, true, true);
    send_peer(&sys, 2u, true, true);

    nav_event_t event = {
        .type = NAV_EVT_PEER_TELEMETRY_RX,
        .timestamp_ms = 1000u,
        .data.peer_beacon_rx = {
            .telemetry = {0},
            .rssi_dbm = -63,
            .snr_db = 9,
        },
    };
    event.data.peer_beacon_rx.telemetry = sample_telemetry(3u);
    event.data.peer_beacon_rx.telemetry.gnss_valid = false;
    nav_core_handle_event(&sys, &event);
    event.type = NAV_EVT_RANGE_RESULT;
    event.data.range_result = sample_range(3u);
    nav_core_handle_event(&sys, &event);

    nav_snapshot_t snapshot = tick_and_snapshot(&sys, 1000u);
    CHECK(snapshot.solution_status == NAV_SOLUTION_REJECTED);
    CHECK(snapshot.reject_reason == NAV_REJECT_NOT_ENOUGH_ANCHORS);
    CHECK(logs_contain(&logs, "reason=BAD_GNSS"));
}

static void test_radio_3d_reject_missing_altitude(void)
{
    nav_system_t sys;
    log_capture_t logs = {0};
    init_system(&sys, &logs);
    send_peer(&sys, 1u, true, true);
    send_peer(&sys, 2u, true, true);
    send_peer(&sys, 3u, true, true);

    nav_snapshot_t snapshot = tick_and_snapshot(&sys, 1000u);
    CHECK(snapshot.solution_status == NAV_SOLUTION_REJECTED);
    CHECK(snapshot.solution_source == NAV_SOURCE_NONE);
    CHECK(snapshot.reject_reason == NAV_REJECT_MISSING_LOCAL_ALTITUDE);
}

static void test_forced_denied_ignores_local_gnss_position(void)
{
    nav_system_t sys;
    log_capture_t logs = {0};
    init_system(&sys, &logs);
    nav_event_t event = local_gnss_event(1000u);
    nav_core_handle_event(&sys, &event);
    send_altitude(&sys);
    send_peer(&sys, 1u, true, true);
    send_peer(&sys, 2u, true, true);
    send_peer(&sys, 3u, true, true);

    nav_snapshot_t snapshot = tick_and_snapshot(&sys, 1000u);
    CHECK(snapshot.solution_status == NAV_SOLUTION_RADIO_3D);
    CHECK(snapshot.solution_source == NAV_SOURCE_RADIO_3D);
    CHECK(snapshot.solution_source != NAV_SOURCE_LOCAL_GNSS);
    CHECK(snapshot.nav_mode == NAV_MODE_RADIO_NAV_OK);
    CHECK(snapshot.position.lat_e7 != event.data.local_gnss.position.lat_e7);
    CHECK(snapshot.altitude_source == NAV_ALT_SOURCE_SIM);
}

static void test_radio_solution_residual_rejected(void)
{
    nav_system_t sys;
    log_capture_t logs = {0};
    init_system(&sys, &logs);
    send_altitude(&sys);
    send_peer(&sys, 1u, true, true);
    send_peer(&sys, 2u, true, true);

    nav_event_t event = {
        .type = NAV_EVT_PEER_TELEMETRY_RX,
        .timestamp_ms = 1000u,
        .data.peer_beacon_rx = {
            .telemetry = {0},
            .rssi_dbm = -63,
            .snr_db = 9,
        },
    };
    event.data.peer_beacon_rx.telemetry = sample_telemetry(3u);
    nav_core_handle_event(&sys, &event);
    event.type = NAV_EVT_RANGE_RESULT;
    event.data.range_result = sample_range(3u);
    event.data.range_result.range_mm += 50000u;
    nav_core_handle_event(&sys, &event);

    nav_snapshot_t snapshot = tick_and_snapshot(&sys, 1000u);
    CHECK(snapshot.solution_status == NAV_SOLUTION_REJECTED);
    CHECK(snapshot.reject_reason == NAV_REJECT_TRILATERATION_FAILED);
    CHECK(logs_contain(&logs, "reason=TRILATERATION_FAILED"));
}

static void test_beacon_rx_metadata_reaches_peer_diagnostics(void)
{
    nav_system_t sys;
    log_capture_t logs = {0};
    init_system(&sys, &logs);

    nav_event_t event = {
        .type = NAV_EVT_PEER_TELEMETRY_RX,
        .timestamp_ms = 1000u,
        .data.peer_beacon_rx = {
            .telemetry = {0},
            .rssi_dbm = -82,
            .snr_db = 4,
        },
    };
    event.data.peer_beacon_rx.telemetry = sample_telemetry(1u);
    event.data.peer_beacon_rx.telemetry.packet_seq = 123456u;
    nav_core_handle_event(&sys, &event);

    const nav_peer_state_t *peer = nav_peer_table_get_const(&sys.peer_table, 1u);
    CHECK(peer != NULL);
    CHECK(peer->packet_seq == 123456u);
    CHECK(peer->rssi_dbm == -82);
    CHECK(peer->snr_db == 4);
    CHECK(logs_contain(&logs, "packet_seq=123456"));
}

static void test_packet_seq_and_request_id_are_not_mixed(void)
{
    nav_system_t sys;
    log_capture_t logs = {0};
    init_system(&sys, &logs);

    nav_event_t event = {
        .type = NAV_EVT_PEER_TELEMETRY_RX,
        .timestamp_ms = 1000u,
        .data.peer_beacon_rx = {
            .telemetry = {0},
            .rssi_dbm = -70,
            .snr_db = 7,
        },
    };
    event.data.peer_beacon_rx.telemetry = sample_telemetry(2u);
    event.data.peer_beacon_rx.telemetry.packet_seq = 1001u;
    nav_core_handle_event(&sys, &event);

    event.type = NAV_EVT_RANGE_RESULT;
    event.data.range_result = sample_range(2u);
    event.data.range_result.request_id = 77u;
    nav_core_handle_event(&sys, &event);

    const nav_peer_state_t *peer = nav_peer_table_get_const(&sys.peer_table, 2u);
    CHECK(peer != NULL);
    CHECK(peer->packet_seq == 1001u);
    CHECK(peer->last_range_request_id == 77u);
}

static void test_range_failure_uses_range_fail_reason(void)
{
    nav_system_t sys;
    log_capture_t logs = {0};
    init_system(&sys, &logs);

    nav_event_t event = {
        .type = NAV_EVT_RANGE_FAIL,
        .timestamp_ms = 1000u,
        .data.range_failure = {
            .peer_id = 1u,
            .request_id = 55u,
            .timestamp_ms = 1000u,
            .reason = NAV_RANGE_FAIL_TIMEOUT,
        },
    };
    nav_core_handle_event(&sys, &event);

    const nav_peer_state_t *peer = nav_peer_table_get_const(&sys.peer_table, 1u);
    CHECK(peer != NULL);
    CHECK(peer->last_range_request_id == 55u);
    CHECK(!peer->range_valid);
    CHECK(logs_contain(&logs, "range_fail_reason=TIMEOUT"));
}

static void test_radio_3d_reject_bad_position(void)
{
    nav_system_t sys;
    log_capture_t logs = {0};
    init_system(&sys, &logs);
    send_altitude(&sys);
    send_peer(&sys, 1u, true, true);
    send_peer(&sys, 2u, true, true);

    nav_event_t event = {
        .type = NAV_EVT_PEER_TELEMETRY_RX,
        .timestamp_ms = 1000u,
        .data.peer_beacon_rx = {
            .telemetry = {0},
            .rssi_dbm = -63,
            .snr_db = 9,
        },
    };
    event.data.peer_beacon_rx.telemetry = sample_telemetry(3u);
    event.data.peer_beacon_rx.telemetry.position.lat_e7 = 1000000000;
    nav_core_handle_event(&sys, &event);

    event.type = NAV_EVT_RANGE_RESULT;
    event.data.range_result = sample_range(3u);
    nav_core_handle_event(&sys, &event);

    nav_snapshot_t snapshot = tick_and_snapshot(&sys, 1000u);
    CHECK(snapshot.solution_status == NAV_SOLUTION_REJECTED);
    CHECK(snapshot.reject_reason == NAV_REJECT_NOT_ENOUGH_ANCHORS);
    CHECK(logs_contain(&logs, "reason=BAD_POSITION"));
}

int main(void)
{
    RUN_TEST(test_radio_3d_solution_exact);
    RUN_TEST(test_radio_3d_reject_not_enough_anchors);
    RUN_TEST(test_radio_3d_reject_stale_telemetry);
    RUN_TEST(test_radio_3d_reject_stale_range);
    RUN_TEST(test_radio_3d_reject_bad_peer_gnss);
    RUN_TEST(test_radio_3d_reject_missing_altitude);
    RUN_TEST(test_forced_denied_ignores_local_gnss_position);
    RUN_TEST(test_radio_solution_residual_rejected);
    RUN_TEST(test_beacon_rx_metadata_reaches_peer_diagnostics);
    RUN_TEST(test_packet_seq_and_request_id_are_not_mixed);
    RUN_TEST(test_range_failure_uses_range_fail_reason);
    RUN_TEST(test_radio_3d_reject_bad_position);
    return 0;
}
