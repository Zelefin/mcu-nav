#include "nav/nav_telemetry.h"

#include <string.h>

/* ---- little-endian cursor helpers ---------------------------------------- */

static size_t put_u8(uint8_t *b, size_t o, uint8_t v)
{
    b[o] = v;
    return o + 1u;
}

static size_t put_u16(uint8_t *b, size_t o, uint16_t v)
{
    b[o] = (uint8_t)(v & 0xFFu);
    b[o + 1u] = (uint8_t)(v >> 8);
    return o + 2u;
}

static size_t put_u32(uint8_t *b, size_t o, uint32_t v)
{
    b[o] = (uint8_t)(v & 0xFFu);
    b[o + 1u] = (uint8_t)((v >> 8) & 0xFFu);
    b[o + 2u] = (uint8_t)((v >> 16) & 0xFFu);
    b[o + 3u] = (uint8_t)((v >> 24) & 0xFFu);
    return o + 4u;
}

static size_t put_i16(uint8_t *b, size_t o, int16_t v)
{
    return put_u16(b, o, (uint16_t)v);
}

static size_t put_i32(uint8_t *b, size_t o, int32_t v)
{
    return put_u32(b, o, (uint32_t)v);
}

static size_t get_u8(const uint8_t *b, size_t o, uint8_t *v)
{
    *v = b[o];
    return o + 1u;
}

static size_t get_u16(const uint8_t *b, size_t o, uint16_t *v)
{
    *v = (uint16_t)b[o] | ((uint16_t)b[o + 1u] << 8);
    return o + 2u;
}

static size_t get_u32(const uint8_t *b, size_t o, uint32_t *v)
{
    *v = (uint32_t)b[o] | ((uint32_t)b[o + 1u] << 8) | ((uint32_t)b[o + 2u] << 16) |
         ((uint32_t)b[o + 3u] << 24);
    return o + 4u;
}

static size_t get_i16(const uint8_t *b, size_t o, int16_t *v)
{
    uint16_t u = 0u;
    o = get_u16(b, o, &u);
    *v = (int16_t)u;
    return o;
}

static size_t get_i32(const uint8_t *b, size_t o, int32_t *v)
{
    uint32_t u = 0u;
    o = get_u32(b, o, &u);
    *v = (int32_t)u;
    return o;
}

#define NAV_TELEMETRY_BEACON_LEN 43u
#define NAV_TELEMETRY_RANGE_RESULT_LEN 21u
#define NAV_TELEMETRY_RANGE_FAIL_LEN 13u

/* ---- encode --------------------------------------------------------------- */

nav_status_t nav_telemetry_encode_beacon(
    const nav_peer_telemetry_t *telemetry,
    uint16_t packet_seq,
    uint8_t *out,
    size_t out_capacity,
    size_t *out_len
)
{
    if (telemetry == NULL || out == NULL || out_len == NULL) {
        return NAV_STATUS_INVALID_ARGUMENT;
    }
    uint8_t payload[NAV_TELEMETRY_BEACON_LEN];
    size_t o = 0u;
    o = put_u8(payload, o, telemetry->node_id);
    o = put_u32(payload, o, telemetry->packet_seq);
    o = put_u32(payload, o, telemetry->timestamp_ms);
    o = put_i32(payload, o, telemetry->position.lat_e7);
    o = put_i32(payload, o, telemetry->position.lon_e7);
    o = put_i32(payload, o, telemetry->position.alt_mm);
    o = put_i32(payload, o, telemetry->velocity.vel_n_mmps);
    o = put_i32(payload, o, telemetry->velocity.vel_e_mmps);
    o = put_i32(payload, o, telemetry->velocity.vel_d_mmps);
    o = put_u8(payload, o, (uint8_t)telemetry->fix_type);
    o = put_u8(payload, o, telemetry->gnss_valid ? 1u : 0u);
    o = put_u8(payload, o, (uint8_t)telemetry->nav_mode);
    o = put_u8(payload, o, (uint8_t)telemetry->solution_status);
    o = put_u8(payload, o, (uint8_t)telemetry->solution_source);
    o = put_i16(payload, o, 0); /* rssi: measured by receiver, not the sender */
    o = put_i16(payload, o, 0); /* snr: measured by receiver, not the sender */
    o = put_u8(payload, o, 0u); /* reserved */

    const nav_radio_frame_t frame = {
        .type = NAV_RADIO_MSG_BEACON_RX,
        .frame_seq = packet_seq,
        .payload = payload,
        .payload_len = o,
    };
    return nav_radio_encode_frame(&frame, out, out_capacity, out_len);
}

nav_status_t nav_telemetry_encode_range_result(
    const nav_range_result_t *range,
    uint16_t frame_seq,
    uint8_t *out,
    size_t out_capacity,
    size_t *out_len
)
{
    if (range == NULL || out == NULL || out_len == NULL) {
        return NAV_STATUS_INVALID_ARGUMENT;
    }
    uint8_t payload[NAV_TELEMETRY_RANGE_RESULT_LEN];
    size_t o = 0u;
    o = put_u8(payload, o, range->peer_id);
    o = put_u8(payload, o, range->valid ? 0u : 1u); /* range_status: 0 == ok */
    o = put_u32(payload, o, range->timestamp_ms);
    o = put_u32(payload, o, range->range_mm);
    o = put_u32(payload, o, range->range_sigma_mm);
    o = put_i16(payload, o, range->rssi_dbm);
    o = put_i16(payload, o, range->snr_db);
    o = put_u8(payload, o, 1u); /* attempt_count */
    o = put_u16(payload, o, range->request_id);

    const nav_radio_frame_t frame = {
        .type = NAV_RADIO_MSG_RANGE_RESULT,
        .frame_seq = frame_seq,
        .payload = payload,
        .payload_len = o,
    };
    return nav_radio_encode_frame(&frame, out, out_capacity, out_len);
}

/* ---- decode --------------------------------------------------------------- */

static nav_status_t decode_beacon(
    const nav_radio_frame_t *frame,
    uint32_t now_ms,
    int16_t rssi_dbm,
    int16_t snr_db,
    nav_event_t *out_event
)
{
    if (frame->payload_len < NAV_TELEMETRY_BEACON_LEN) {
        return NAV_STATUS_BAD_FRAME;
    }
    const uint8_t *p = frame->payload;
    nav_peer_telemetry_t t = {0};
    uint8_t fix = 0u;
    uint8_t valid = 0u;
    uint8_t mode = 0u;
    uint8_t solution_status = 0u;
    uint8_t solution_source = 0u;
    int16_t ignored = 0;
    uint8_t reserved = 0u;
    size_t o = 0u;
    o = get_u8(p, o, &t.node_id);
    o = get_u32(p, o, &t.packet_seq);
    o = get_u32(p, o, &t.timestamp_ms);
    o = get_i32(p, o, &t.position.lat_e7);
    o = get_i32(p, o, &t.position.lon_e7);
    o = get_i32(p, o, &t.position.alt_mm);
    o = get_i32(p, o, &t.velocity.vel_n_mmps);
    o = get_i32(p, o, &t.velocity.vel_e_mmps);
    o = get_i32(p, o, &t.velocity.vel_d_mmps);
    o = get_u8(p, o, &fix);
    o = get_u8(p, o, &valid);
    o = get_u8(p, o, &mode);
    o = get_u8(p, o, &solution_status);
    o = get_u8(p, o, &solution_source);
    o = get_i16(p, o, &ignored);
    o = get_i16(p, o, &ignored);
    (void)get_u8(p, o, &reserved);

    t.fix_type = (nav_gnss_fix_type_t)fix;
    t.gnss_valid = valid != 0u;
    t.nav_mode = (nav_mode_t)mode;
    t.solution_status = (nav_solution_status_t)solution_status;
    t.solution_source = (nav_solution_source_t)solution_source;

    out_event->type = NAV_EVT_PEER_TELEMETRY_RX;
    out_event->timestamp_ms = now_ms;
    out_event->data.peer_beacon_rx.telemetry = t;
    out_event->data.peer_beacon_rx.rssi_dbm = rssi_dbm;
    out_event->data.peer_beacon_rx.snr_db = snr_db;
    return NAV_STATUS_OK;
}

static nav_status_t decode_range_result(
    const nav_radio_frame_t *frame,
    uint32_t now_ms,
    nav_event_t *out_event
)
{
    if (frame->payload_len < NAV_TELEMETRY_RANGE_RESULT_LEN) {
        return NAV_STATUS_BAD_FRAME;
    }
    const uint8_t *p = frame->payload;
    nav_range_result_t r = {0};
    uint8_t status = 0u;
    uint8_t attempt = 0u;
    size_t o = 0u;
    o = get_u8(p, o, &r.peer_id);
    o = get_u8(p, o, &status);
    o = get_u32(p, o, &r.timestamp_ms);
    o = get_u32(p, o, &r.range_mm);
    o = get_u32(p, o, &r.range_sigma_mm);
    o = get_i16(p, o, &r.rssi_dbm);
    o = get_i16(p, o, &r.snr_db);
    o = get_u8(p, o, &attempt);
    (void)get_u16(p, o, &r.request_id);
    r.valid = status == 0u;

    out_event->type = NAV_EVT_RANGE_RESULT;
    out_event->timestamp_ms = now_ms;
    out_event->data.range_result = r;
    return NAV_STATUS_OK;
}

static nav_status_t decode_range_fail(
    const nav_radio_frame_t *frame,
    uint32_t now_ms,
    nav_event_t *out_event
)
{
    if (frame->payload_len < NAV_TELEMETRY_RANGE_FAIL_LEN) {
        return NAV_STATUS_BAD_FRAME;
    }
    const uint8_t *p = frame->payload;
    nav_range_failure_t f = {0};
    uint8_t reason = 0u;
    uint8_t attempt = 0u;
    int16_t ignored = 0;
    size_t o = 0u;
    o = get_u8(p, o, &f.peer_id);
    o = get_u8(p, o, &reason);
    o = get_u32(p, o, &f.timestamp_ms);
    o = get_u8(p, o, &attempt);
    o = get_i16(p, o, &ignored);
    o = get_i16(p, o, &ignored);
    (void)get_u16(p, o, &f.request_id);
    f.reason = (nav_range_fail_reason_t)reason;

    out_event->type = NAV_EVT_RANGE_FAIL;
    out_event->timestamp_ms = now_ms;
    out_event->data.range_failure = f;
    return NAV_STATUS_OK;
}

nav_status_t nav_telemetry_decode_event(
    const uint8_t *bytes,
    size_t len,
    uint32_t now_ms,
    int16_t rssi_dbm,
    int16_t snr_db,
    nav_event_t *out_event
)
{
    if (bytes == NULL || out_event == NULL) {
        return NAV_STATUS_INVALID_ARGUMENT;
    }
    nav_radio_frame_t frame;
    const nav_status_t decoded = nav_radio_decode_frame(bytes, len, &frame);
    if (decoded != NAV_STATUS_OK) {
        return decoded;
    }
    memset(out_event, 0, sizeof(*out_event));

    switch (frame.type) {
    case NAV_RADIO_MSG_BEACON_RX:
        return decode_beacon(&frame, now_ms, rssi_dbm, snr_db, out_event);
    case NAV_RADIO_MSG_RANGE_RESULT:
        return decode_range_result(&frame, now_ms, out_event);
    case NAV_RADIO_MSG_RANGE_FAIL:
        return decode_range_fail(&frame, now_ms, out_event);
    default:
        return NAV_STATUS_NOT_IMPLEMENTED;
    }
}
