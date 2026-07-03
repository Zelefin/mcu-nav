#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "nav/nav_radio_protocol.h"

int main(void)
{
    nav_radio_frame_t decoded;
    const uint8_t invalid[] = {0x00u, 0x01u, 0x02u};
    assert(nav_radio_decode_frame(invalid, sizeof(invalid), &decoded) == NAV_STATUS_BAD_FRAME);
    assert(nav_radio_decode_frame(0, sizeof(invalid), &decoded) == NAV_STATUS_INVALID_ARGUMENT);

    uint8_t encoded[16];
    size_t encoded_len = 0u;
    const uint8_t payload[] = {1u, 2u, 3u};
    nav_radio_frame_t frame = {
        .type = NAV_RADIO_MSG_HEARTBEAT,
        .frame_seq = 7u,
        .payload = payload,
        .payload_len = sizeof(payload),
    };
    assert(nav_radio_encode_frame(&frame, encoded, sizeof(encoded), &encoded_len) == NAV_STATUS_OK);
    assert(nav_radio_decode_frame(encoded, encoded_len, &decoded) == NAV_STATUS_OK);
    assert(decoded.type == NAV_RADIO_MSG_HEARTBEAT);
    assert(decoded.frame_seq == 7u);
    assert(decoded.payload_len == sizeof(payload));
    assert(memcmp(decoded.payload, payload, sizeof(payload)) == 0);
    assert(strcmp(nav_radio_message_type_to_string(NAV_RADIO_MSG_RANGE_RESULT), "RANGE_RESULT") == 0);
    assert(NAV_RADIO_MSG_DEBUG_ENABLE == 71);
    assert(NAV_RADIO_MSG_NODE_QUALITY_REPORT == 72);
    assert(strcmp(nav_radio_message_type_to_string(NAV_RADIO_MSG_DEBUG_ENABLE), "DEBUG_ENABLE") == 0);
    assert(strcmp(nav_radio_message_type_to_string(NAV_RADIO_MSG_NODE_QUALITY_REPORT), "NODE_QUALITY_REPORT") == 0);

    nav_radio_range_result_payload_t range_payload = {
        .peer_id_u8 = 2u,
        .range_status_u8 = 0u,
        .radio_timestamp_ms_u32 = 1000u,
        .range_mm_u32 = 621957u,
        .range_sigma_mm_u32 = 100u,
        .rssi_dbm_i16 = -61,
        .snr_db_i16 = 10,
        .attempt_count_u8 = 1u,
        .request_id_u16 = 77u,
    };
    assert(range_payload.range_mm_u32 == 621957u);
    assert(range_payload.snr_db_i16 == 10);
    assert(range_payload.request_id_u16 == 77u);

    nav_radio_debug_enable_payload_t debug_payload = {
        .origin_node_id_u8 = 1u,
        .ttl_ms_u16 = 1500u,
    };
    assert(debug_payload.origin_node_id_u8 == 1u);
    assert(debug_payload.ttl_ms_u16 == 1500u);

    nav_radio_node_quality_report_payload_t quality_payload = {
        .node_id_u8 = 3u,
        .nav_mode_u8 = 6u,
        .solution_status_u8 = 2u,
        .solution_source_u8 = 2u,
        .num_anchors_u8 = 3u,
        .anchor_ids_u8 = {0u, 1u, 2u},
        .fix_type_u8 = 3u,
        .satellites_u8 = 12u,
        .geometry_score_u8 = 222u,
        .total_quality_u8 = 201u,
        .residual_rms_mm_u16 = 310u,
        .max_residual_mm_u16 = 520u,
        .hdop_centi_u16 = 85u,
        .hacc_mm_u32 = 1200u,
        .vacc_mm_u32 = 2100u,
        .lat_e7_i32 = 504529000,
        .lon_e7_i32 = 305268000,
        .alt_mm_i32 = 183500,
        .packet_seq_u32 = 104u,
    };
    assert(quality_payload.anchor_ids_u8[2] == 2u);
    assert(quality_payload.packet_seq_u32 == 104u);
    return 0;
}
