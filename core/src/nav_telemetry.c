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
#define NAV_TELEMETRY_DEBUG_ENABLE_LEN 3u
#define NAV_TELEMETRY_HEARTBEAT_LEN 16u
#define NAV_TELEMETRY_NODE_QUALITY_REPORT_LEN 42u

static uint8_t clamp_quality_to_u8(float value)
{
    if (value <= 0.0f) {
        return 0u;
    }
    if (value >= 1.0f) {
        return 255u;
    }
    return (uint8_t)((value * 255.0f) + 0.5f);
}

static float quality_from_u8(uint8_t value)
{
    return (float)value / 255.0f;
}

static uint16_t saturate_u16(uint32_t value)
{
    return value > 65535u ? 65535u : (uint16_t)value;
}

static uint8_t clamp_anchor_count(uint8_t count)
{
    return count > NAV_TRILAT_ANCHOR_COUNT ? NAV_TRILAT_ANCHOR_COUNT : count;
}

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

nav_status_t nav_telemetry_encode_debug_enable(
    const nav_debug_enable_t *debug,
    uint16_t frame_seq,
    uint8_t *out,
    size_t out_capacity,
    size_t *out_len
)
{
    if (debug == NULL || out == NULL || out_len == NULL) {
        return NAV_STATUS_INVALID_ARGUMENT;
    }
    uint8_t payload[NAV_TELEMETRY_DEBUG_ENABLE_LEN];
    size_t o = 0u;
    o = put_u8(payload, o, debug->origin_node_id);
    o = put_u16(payload, o, debug->ttl_ms);

    const nav_radio_frame_t frame = {
        .type = NAV_RADIO_MSG_DEBUG_ENABLE,
        .frame_seq = frame_seq,
        .payload = payload,
        .payload_len = o,
    };
    return nav_radio_encode_frame(&frame, out, out_capacity, out_len);
}

nav_status_t nav_telemetry_encode_heartbeat(
    const nav_radio_heartbeat_payload_t *heartbeat,
    uint16_t frame_seq,
    uint8_t *out,
    size_t out_capacity,
    size_t *out_len
)
{
    if (heartbeat == NULL || out == NULL || out_len == NULL) {
        return NAV_STATUS_INVALID_ARGUMENT;
    }
    uint8_t payload[NAV_TELEMETRY_HEARTBEAT_LEN];
    size_t o = 0u;
    o = put_u8(payload, o, heartbeat->node_id_u8);
    o = put_u32(payload, o, heartbeat->uptime_ms_u32);
    o = put_u32(payload, o, heartbeat->status_flags_u32);
    o = put_u32(payload, o, heartbeat->tdma_frame_index_u32);
    o = put_u8(payload, o, heartbeat->tdma_slot_index_u8);
    o = put_u16(payload, o, heartbeat->tdma_slot_ms_u16);

    const nav_radio_frame_t frame = {
        .type = NAV_RADIO_MSG_HEARTBEAT,
        .frame_seq = frame_seq,
        .payload = payload,
        .payload_len = o,
    };
    return nav_radio_encode_frame(&frame, out, out_capacity, out_len);
}

nav_status_t nav_telemetry_encode_node_quality_report(
    const nav_node_quality_report_t *report,
    uint16_t frame_seq,
    uint8_t *out,
    size_t out_capacity,
    size_t *out_len
)
{
    if (report == NULL || out == NULL || out_len == NULL) {
        return NAV_STATUS_INVALID_ARGUMENT;
    }

    uint8_t payload[NAV_TELEMETRY_NODE_QUALITY_REPORT_LEN];
    const uint8_t num_anchors = clamp_anchor_count(report->num_anchors);
    size_t o = 0u;
    o = put_u8(payload, o, report->node_id);
    o = put_u8(payload, o, (uint8_t)report->nav_mode);
    o = put_u8(payload, o, (uint8_t)report->solution_status);
    o = put_u8(payload, o, (uint8_t)report->solution_source);
    o = put_u8(payload, o, num_anchors);
    for (size_t i = 0u; i < NAV_TRILAT_ANCHOR_COUNT; ++i) {
        o = put_u8(payload, o, report->anchor_ids[i]);
    }
    o = put_u8(payload, o, (uint8_t)report->fix_type);
    o = put_u8(payload, o, report->satellites);
    o = put_u8(payload, o, clamp_quality_to_u8(report->geometry_score));
    o = put_u8(payload, o, clamp_quality_to_u8(report->total_quality));
    o = put_u16(payload, o, saturate_u16(report->residual_rms_mm));
    o = put_u16(payload, o, saturate_u16(report->max_residual_mm));
    o = put_u16(payload, o, report->hdop_centi);
    o = put_u32(payload, o, report->hacc_mm);
    o = put_u32(payload, o, report->vacc_mm);
    o = put_i32(payload, o, report->position.lat_e7);
    o = put_i32(payload, o, report->position.lon_e7);
    o = put_i32(payload, o, report->position.alt_mm);
    o = put_u32(payload, o, report->packet_seq);

    const nav_radio_frame_t frame = {
        .type = NAV_RADIO_MSG_NODE_QUALITY_REPORT,
        .frame_seq = frame_seq,
        .payload = payload,
        .payload_len = o,
    };
    return nav_radio_encode_frame(&frame, out, out_capacity, out_len);
}

/* ---- decode --------------------------------------------------------------- */

nav_status_t nav_telemetry_decode_debug_enable(const nav_radio_frame_t *frame, nav_debug_enable_t *out)
{
    if (frame == NULL || out == NULL) {
        return NAV_STATUS_INVALID_ARGUMENT;
    }
    if (frame->type != NAV_RADIO_MSG_DEBUG_ENABLE) {
        return NAV_STATUS_NOT_IMPLEMENTED;
    }
    if (frame->payload_len < NAV_TELEMETRY_DEBUG_ENABLE_LEN) {
        return NAV_STATUS_BAD_FRAME;
    }

    memset(out, 0, sizeof(*out));
    size_t o = 0u;
    o = get_u8(frame->payload, o, &out->origin_node_id);
    (void)get_u16(frame->payload, o, &out->ttl_ms);
    return NAV_STATUS_OK;
}

nav_status_t nav_telemetry_decode_heartbeat(
    const nav_radio_frame_t *frame,
    nav_radio_heartbeat_payload_t *out
)
{
    if (frame == NULL || out == NULL) {
        return NAV_STATUS_INVALID_ARGUMENT;
    }
    if (frame->type != NAV_RADIO_MSG_HEARTBEAT) {
        return NAV_STATUS_NOT_IMPLEMENTED;
    }
    if (frame->payload_len < NAV_TELEMETRY_HEARTBEAT_LEN) {
        return NAV_STATUS_BAD_FRAME;
    }

    memset(out, 0, sizeof(*out));
    size_t o = 0u;
    o = get_u8(frame->payload, o, &out->node_id_u8);
    o = get_u32(frame->payload, o, &out->uptime_ms_u32);
    o = get_u32(frame->payload, o, &out->status_flags_u32);
    o = get_u32(frame->payload, o, &out->tdma_frame_index_u32);
    o = get_u8(frame->payload, o, &out->tdma_slot_index_u8);
    (void)get_u16(frame->payload, o, &out->tdma_slot_ms_u16);
    return NAV_STATUS_OK;
}

nav_status_t nav_telemetry_decode_node_quality_report(
    const nav_radio_frame_t *frame,
    nav_node_quality_report_t *out
)
{
    if (frame == NULL || out == NULL) {
        return NAV_STATUS_INVALID_ARGUMENT;
    }
    if (frame->type != NAV_RADIO_MSG_NODE_QUALITY_REPORT) {
        return NAV_STATUS_NOT_IMPLEMENTED;
    }
    if (frame->payload_len < NAV_TELEMETRY_NODE_QUALITY_REPORT_LEN) {
        return NAV_STATUS_BAD_FRAME;
    }

    memset(out, 0, sizeof(*out));
    uint8_t nav_mode = 0u;
    uint8_t solution_status = 0u;
    uint8_t solution_source = 0u;
    uint8_t fix_type = 0u;
    uint8_t geometry_score = 0u;
    uint8_t total_quality = 0u;
    uint16_t residual_rms_mm = 0u;
    uint16_t max_residual_mm = 0u;
    size_t o = 0u;
    o = get_u8(frame->payload, o, &out->node_id);
    o = get_u8(frame->payload, o, &nav_mode);
    o = get_u8(frame->payload, o, &solution_status);
    o = get_u8(frame->payload, o, &solution_source);
    o = get_u8(frame->payload, o, &out->num_anchors);
    out->num_anchors = clamp_anchor_count(out->num_anchors);
    for (size_t i = 0u; i < NAV_TRILAT_ANCHOR_COUNT; ++i) {
        o = get_u8(frame->payload, o, &out->anchor_ids[i]);
    }
    o = get_u8(frame->payload, o, &fix_type);
    o = get_u8(frame->payload, o, &out->satellites);
    o = get_u8(frame->payload, o, &geometry_score);
    o = get_u8(frame->payload, o, &total_quality);
    o = get_u16(frame->payload, o, &residual_rms_mm);
    o = get_u16(frame->payload, o, &max_residual_mm);
    o = get_u16(frame->payload, o, &out->hdop_centi);
    o = get_u32(frame->payload, o, &out->hacc_mm);
    o = get_u32(frame->payload, o, &out->vacc_mm);
    o = get_i32(frame->payload, o, &out->position.lat_e7);
    o = get_i32(frame->payload, o, &out->position.lon_e7);
    o = get_i32(frame->payload, o, &out->position.alt_mm);
    (void)get_u32(frame->payload, o, &out->packet_seq);

    out->nav_mode = (nav_mode_t)nav_mode;
    out->solution_status = (nav_solution_status_t)solution_status;
    out->solution_source = (nav_solution_source_t)solution_source;
    out->fix_type = (nav_gnss_fix_type_t)fix_type;
    out->geometry_score = quality_from_u8(geometry_score);
    out->total_quality = quality_from_u8(total_quality);
    out->residual_rms_mm = residual_rms_mm;
    out->max_residual_mm = max_residual_mm;
    return NAV_STATUS_OK;
}

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
