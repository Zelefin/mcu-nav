#include <assert.h>
#include <string.h>

#include "nav/nav_serial_json.h"

static void test_parse(void)
{
    nav_ctrl_command_t cmd;

    assert(nav_serial_parse_command("{\"cmd\":\"get\"}", &cmd) == NAV_STATUS_OK);
    assert(cmd.type == NAV_CTRL_CMD_GET);

    assert(nav_serial_parse_command("  {\"cmd\": \"gps\", \"enabled\": false}", &cmd) == NAV_STATUS_OK);
    assert(cmd.type == NAV_CTRL_CMD_SET_GPS);
    assert(cmd.bool_value == false);

    assert(nav_serial_parse_command("{\"cmd\":\"mock\",\"enabled\":true}", &cmd) == NAV_STATUS_OK);
    assert(cmd.type == NAV_CTRL_CMD_SET_MOCK);
    assert(cmd.bool_value == true);

    assert(nav_serial_parse_command("{\"cmd\":\"name\",\"value\":\"alpha-1\"}", &cmd) == NAV_STATUS_OK);
    assert(cmd.type == NAV_CTRL_CMD_SET_NAME);
    assert(strcmp(cmd.str_value, "alpha-1") == 0);

    assert(nav_serial_parse_command("{\"cmd\":\"alt\",\"alt_mm\":183500}", &cmd) == NAV_STATUS_OK);
    assert(cmd.type == NAV_CTRL_CMD_SET_ALTITUDE);
    assert(cmd.int_value == 183500);

    assert(nav_serial_parse_command("{\"cmd\":\"node_id\",\"id\":3}", &cmd) == NAV_STATUS_OK);
    assert(cmd.type == NAV_CTRL_CMD_SET_NODE_ID);
    assert(cmd.int_value == 3);

    assert(nav_serial_parse_command("{\"cmd\":\"node_id\",\"id\":4}", &cmd) == NAV_STATUS_BAD_FRAME);

    assert(nav_serial_parse_command("{\"cmd\":\"frobnicate\"}", &cmd) == NAV_STATUS_OK);
    assert(cmd.type == NAV_CTRL_CMD_UNKNOWN);

    assert(nav_serial_parse_command("not json", &cmd) == NAV_STATUS_BAD_FRAME);
    assert(nav_serial_parse_command("{\"cmd\":\"gps\"}", &cmd) == NAV_STATUS_BAD_FRAME);
}

static void test_write(void)
{
    nav_snapshot_t snapshot = {0};
    snapshot.time_ms = 1000u;
    snapshot.node_id = 0u;
    snapshot.nav_mode = NAV_MODE_RADIO_NAV_OK;
    snapshot.solution_status = NAV_SOLUTION_RADIO_3D;
    snapshot.solution_source = NAV_SOURCE_RADIO_3D;
    snapshot.reject_reason = NAV_REJECT_NONE;
    snapshot.position = (nav_position_t){504520000, 305260000, 183500};
    snapshot.num_anchors = 3u;

    nav_peer_table_t peers;
    nav_peer_table_init(&peers);
    nav_peer_beacon_rx_t beacon = {
        .telemetry = {
            .node_id = 1u,
            .packet_seq = 7u,
            .timestamp_ms = 1000u,
            .position = {504501000, 305234000, 180000},
            .fix_type = NAV_GNSS_FIX_3D,
            .gnss_valid = true,
            .satellites = 12u,
            .nav_mode = NAV_MODE_GNSS_OK,
        },
        .rssi_dbm = -60,
        .snr_db = 10,
    };
    assert(nav_peer_table_update_beacon_rx(&peers, &beacon, 1000u));
    nav_range_result_t range = {
        .peer_id = 1u,
        .request_id = 9u,
        .timestamp_ms = 1000u,
        .range_mm = 394135u,
        .range_sigma_mm = 100u,
        .rssi_dbm = -60,
        .snr_db = 10,
        .valid = true,
    };
    assert(nav_peer_table_update_range(&peers, &range, 1000u));

    nav_serial_node_info_t info = {
        .node_id = 0u,
        .node_name = "alpha",
        .gps_enabled = false,
        .mock_enabled = true,
    };

    char buf[1024];
    const int n = nav_serial_write_snapshot(buf, sizeof(buf), &info, &snapshot, &peers);
    assert(n > 0);
    assert((size_t)n == strlen(buf));

    assert(strstr(buf, "\"name\":\"alpha\"") != NULL);
    assert(strstr(buf, "\"gps\":false") != NULL);
    assert(strstr(buf, "\"mock\":true") != NULL);
    assert(strstr(buf, "\"mode\":\"RADIO_NAV_OK\"") != NULL);
    assert(strstr(buf, "\"src\":\"RADIO_3D\"") != NULL);
    assert(strstr(buf, "\"lat_e7\":504520000") != NULL);
    assert(strstr(buf, "\"id\":1") != NULL);
    assert(strstr(buf, "\"range_mm\":394135") != NULL);

    /* snprintf semantics: cap=0 reports the needed length without writing. */
    const int needed = nav_serial_write_snapshot(NULL, 0u, &info, &snapshot, &peers);
    assert(needed == n);
}

int main(void)
{
    test_parse();
    test_write();
    return 0;
}
