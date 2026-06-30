#include <assert.h>

#include "nav/nav_telemetry.h"

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
    assert(b->rssi_dbm == -64); /* measured locally on receive */
    assert(b->snr_db == 9);
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
    test_range_roundtrip();
    test_decode_errors();
    return 0;
}
