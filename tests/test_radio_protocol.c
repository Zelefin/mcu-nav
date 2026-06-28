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
    return 0;
}
