#include <assert.h>

#include "nav/nav_telemetry.h"

static void assert_quality_near(float actual, float expected)
{
    const float delta = actual > expected ? actual - expected : expected - actual;
    assert(delta < 0.003f);
}

static void test_beacon_roundtrip(void)
{
    nav_peer_telemetry_t t = {
        .node_id = 3u,
        .packet_seq = 104u,
        .timestamp_ms = 123500u,
        .position = {504510000, 305340000, 175000},
        .velocity = {10, -20, 5},
        .fix_type = NAV_GNSS_FIX_3D,
        .gnss_valid = true,
        .satellites = 11u,
        .nav_mode = NAV_MODE_GNSS_OK,
        .solution_status = NAV_SOLUTION_GNSS_DIRECT,
        .solution_source = NAV_SOURCE_LOCAL_GNSS,
    };

    uint8_t bytes[NAV_RADIO_MAX_FRAME_BYTES];
    size_t len = 0u;
    assert(nav_telemetry_encode_beacon(&t, (uint16_t)t.packet_seq, bytes, sizeof(bytes), &len) == NAV_STATUS_OK);
    assert(len > 6u);

    nav_event_t ev;
    assert(nav_telemetry_decode_event(bytes, len, 200000u, -64, 9, &ev) == NAV_STATUS_OK);
    assert(ev.type == NAV_EVT_PEER_TELEMETRY_RX);
    assert(ev.timestamp_ms == 200000u);
    const nav_peer_beacon_rx_t *b = &ev.data.peer_beacon_rx;
    assert(b->telemetry.node_id == 3u);
    assert(b->telemetry.packet_seq == 104u);
    assert(b->telemetry.position.lat_e7 == 504510000);
    assert(b->telemetry.position.lon_e7 == 305340000);
    assert(b->telemetry.position.alt_mm == 175000);
    assert(b->telemetry.velocity.vel_e_mmps == -20);
    assert(b->telemetry.gnss_valid);
    assert(b->telemetry.fix_type == NAV_GNSS_FIX_3D);
    assert(b->telemetry.nav_mode == NAV_MODE_GNSS_OK);
    assert(b->telemetry.solution_status == NAV_SOLUTION_GNSS_DIRECT);
    assert(b->telemetry.solution_source == NAV_SOURCE_LOCAL_GNSS);
    assert(b->rssi_dbm == -64); /* measured locally on receive */
    assert(b->snr_db == 9);
}

static void test_radio_3d_beacon_roundtrip(void)
{
    nav_peer_telemetry_t t = {
        .node_id = 1u,
        .packet_seq = 205u,
        .timestamp_ms = 125000u,
        .position = {504529000, 305268000, 183500},
        .fix_type = NAV_GNSS_FIX_NONE,
        .gnss_valid = false,
        .nav_mode = NAV_MODE_RADIO_NAV_OK,
        .solution_status = NAV_SOLUTION_RADIO_3D,
        .solution_source = NAV_SOURCE_RADIO_3D,
    };

    uint8_t bytes[NAV_RADIO_MAX_FRAME_BYTES];
    size_t len = 0u;
    assert(nav_telemetry_encode_beacon(&t, (uint16_t)t.packet_seq, bytes, sizeof(bytes), &len) == NAV_STATUS_OK);

    nav_event_t ev;
    assert(nav_telemetry_decode_event(bytes, len, 201000u, -70, 7, &ev) == NAV_STATUS_OK);
    const nav_peer_beacon_rx_t *b = &ev.data.peer_beacon_rx;
    assert(b->telemetry.position.lat_e7 == 504529000);
    assert(!b->telemetry.gnss_valid);
    assert(b->telemetry.fix_type == NAV_GNSS_FIX_NONE);
    assert(b->telemetry.solution_status == NAV_SOLUTION_RADIO_3D);
    assert(b->telemetry.solution_source == NAV_SOURCE_RADIO_3D);
}

static void test_range_roundtrip(void)
{
    nav_range_result_t r = {
        .peer_id = 2u,
        .request_id = 77u,
        .timestamp_ms = 123456u,
        .range_mm = 621957u,
        .range_sigma_mm = 100u,
        .rssi_dbm = -61,
        .snr_db = 10,
        .valid = true,
    };

    uint8_t bytes[NAV_RADIO_MAX_FRAME_BYTES];
    size_t len = 0u;
    assert(nav_telemetry_encode_range_result(&r, 42u, bytes, sizeof(bytes), &len) == NAV_STATUS_OK);

    nav_event_t ev;
    assert(nav_telemetry_decode_event(bytes, len, 130000u, -61, 10, &ev) == NAV_STATUS_OK);
    assert(ev.type == NAV_EVT_RANGE_RESULT);
    const nav_range_result_t *d = &ev.data.range_result;
    assert(d->peer_id == 2u);
    assert(d->request_id == 77u);
    assert(d->range_mm == 621957u);
    assert(d->range_sigma_mm == 100u);
    assert(d->valid);
}

static void test_debug_enable_roundtrip(void)
{
    nav_debug_enable_t debug = {
        .origin_node_id = 2u,
        .ttl_ms = 1500u,
    };

    uint8_t bytes[NAV_RADIO_MAX_FRAME_BYTES];
    size_t len = 0u;
    assert(nav_telemetry_encode_debug_enable(&debug, 9u, bytes, sizeof(bytes), &len) == NAV_STATUS_OK);
    assert(len == 9u);

    nav_radio_frame_t frame;
    assert(nav_radio_decode_frame(bytes, len, &frame) == NAV_STATUS_OK);
    assert(frame.type == NAV_RADIO_MSG_DEBUG_ENABLE);
    assert(frame.frame_seq == 9u);

    nav_debug_enable_t decoded;
    assert(nav_telemetry_decode_debug_enable(&frame, &decoded) == NAV_STATUS_OK);
    assert(decoded.origin_node_id == 2u);
    assert(decoded.ttl_ms == 1500u);

    assert(nav_telemetry_encode_debug_enable(&debug, 9u, bytes, 8u, &len) == NAV_STATUS_BAD_FRAME);
}

static void test_node_quality_roundtrip(void)
{
    nav_node_quality_report_t report = {
        .node_id = 3u,
        .nav_mode = NAV_MODE_RADIO_NAV_OK,
        .solution_status = NAV_SOLUTION_RADIO_3D,
        .solution_source = NAV_SOURCE_RADIO_3D,
        .num_anchors = 3u,
        .anchor_ids = {0u, 1u, 2u},
        .fix_type = NAV_GNSS_FIX_3D,
        .satellites = 12u,
        .geometry_score = 0.87f,
        .total_quality = 0.79f,
        .residual_rms_mm = 310u,
        .max_residual_mm = 520u,
        .hdop_centi = 85u,
        .hacc_mm = 1200u,
        .vacc_mm = 2100u,
        .position = {504529000, 305268000, 183500},
        .packet_seq = 104u,
    };

    uint8_t bytes[NAV_RADIO_MAX_FRAME_BYTES];
    size_t len = 0u;
    assert(nav_telemetry_encode_node_quality_report(&report, 42u, bytes, sizeof(bytes), &len) == NAV_STATUS_OK);
    assert(len == 48u);

    nav_radio_frame_t frame;
    assert(nav_radio_decode_frame(bytes, len, &frame) == NAV_STATUS_OK);
    assert(frame.type == NAV_RADIO_MSG_NODE_QUALITY_REPORT);
    assert(frame.frame_seq == 42u);

    nav_node_quality_report_t decoded;
    assert(nav_telemetry_decode_node_quality_report(&frame, &decoded) == NAV_STATUS_OK);
    assert(decoded.node_id == 3u);
    assert(decoded.nav_mode == NAV_MODE_RADIO_NAV_OK);
    assert(decoded.solution_status == NAV_SOLUTION_RADIO_3D);
    assert(decoded.solution_source == NAV_SOURCE_RADIO_3D);
    assert(decoded.num_anchors == 3u);
    assert(decoded.anchor_ids[0] == 0u);
    assert(decoded.anchor_ids[1] == 1u);
    assert(decoded.anchor_ids[2] == 2u);
    assert(decoded.fix_type == NAV_GNSS_FIX_3D);
    assert(decoded.satellites == 12u);
    assert_quality_near(decoded.geometry_score, 0.87f);
    assert_quality_near(decoded.total_quality, 0.79f);
    assert(decoded.residual_rms_mm == 310u);
    assert(decoded.max_residual_mm == 520u);
    assert(decoded.hdop_centi == 85u);
    assert(decoded.hacc_mm == 1200u);
    assert(decoded.vacc_mm == 2100u);
    assert(decoded.position.lat_e7 == 504529000);
    assert(decoded.position.lon_e7 == 305268000);
    assert(decoded.position.alt_mm == 183500);
    assert(decoded.packet_seq == 104u);
}

static void test_node_quality_bounds(void)
{
    nav_node_quality_report_t report = {
        .node_id = 1u,
        .nav_mode = NAV_MODE_NO_NAV_SOLUTION,
        .solution_status = NAV_SOLUTION_REJECTED,
        .solution_source = NAV_SOURCE_NONE,
        .num_anchors = 9u,
        .anchor_ids = {1u, 2u, 3u},
        .fix_type = NAV_GNSS_FIX_NONE,
        .satellites = 0u,
        .geometry_score = -0.5f,
        .total_quality = 2.0f,
        .residual_rms_mm = 70000u,
        .max_residual_mm = 90000u,
        .packet_seq = 17u,
    };

    uint8_t bytes[NAV_RADIO_MAX_FRAME_BYTES];
    size_t len = 0u;
    assert(nav_telemetry_encode_node_quality_report(&report, 11u, bytes, sizeof(bytes), &len) == NAV_STATUS_OK);

    nav_radio_frame_t frame;
    assert(nav_radio_decode_frame(bytes, len, &frame) == NAV_STATUS_OK);
    nav_node_quality_report_t decoded;
    assert(nav_telemetry_decode_node_quality_report(&frame, &decoded) == NAV_STATUS_OK);
    assert(decoded.num_anchors == NAV_TRILAT_ANCHOR_COUNT);
    assert(decoded.geometry_score == 0.0f);
    assert(decoded.total_quality == 1.0f);
    assert(decoded.residual_rms_mm == 65535u);
    assert(decoded.max_residual_mm == 65535u);

    uint8_t short_frame_bytes[48];
    for (size_t i = 0u; i < sizeof(short_frame_bytes); ++i) {
        short_frame_bytes[i] = bytes[i];
    }
    short_frame_bytes[5] = 41u;
    nav_radio_frame_t short_frame;
    assert(nav_radio_decode_frame(short_frame_bytes, sizeof(short_frame_bytes) - 1u, &short_frame) == NAV_STATUS_OK);
    assert(nav_telemetry_decode_node_quality_report(&short_frame, &decoded) == NAV_STATUS_BAD_FRAME);
}

static void test_decode_errors(void)
{
    nav_event_t ev;
    const uint8_t junk[4] = {0, 1, 2, 3};
    assert(nav_telemetry_decode_event(junk, sizeof(junk), 0u, 0, 0, &ev) == NAV_STATUS_BAD_FRAME);

    /* A well-formed frame of an unmapped type. */
    const uint8_t heartbeat[6] = {0x4e, 0x52, NAV_RADIO_MSG_HEARTBEAT, 0, 0, 0};
    assert(nav_telemetry_decode_event(heartbeat, sizeof(heartbeat), 0u, 0, 0, &ev) == NAV_STATUS_NOT_IMPLEMENTED);
}

int main(void)
{
    test_beacon_roundtrip();
    test_radio_3d_beacon_roundtrip();
    test_range_roundtrip();
    test_debug_enable_roundtrip();
    test_node_quality_roundtrip();
    test_node_quality_bounds();
    test_decode_errors();
    return 0;
}
