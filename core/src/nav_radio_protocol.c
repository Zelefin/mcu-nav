#include "nav/nav_radio_protocol.h"

#define NAV_RADIO_MAGIC0 0x4eu
#define NAV_RADIO_MAGIC1 0x52u
#define NAV_RADIO_HEADER_LEN 6u

nav_status_t nav_radio_decode_frame(const uint8_t *bytes, size_t len, nav_radio_frame_t *out)
{
    if (bytes == NULL || out == NULL) {
        return NAV_STATUS_INVALID_ARGUMENT;
    }
    if (len < NAV_RADIO_HEADER_LEN || bytes[0] != NAV_RADIO_MAGIC0 || bytes[1] != NAV_RADIO_MAGIC1) {
        return NAV_STATUS_BAD_FRAME;
    }
    const size_t payload_len = bytes[5];
    if (payload_len > NAV_RADIO_MAX_FRAME_BYTES || len != NAV_RADIO_HEADER_LEN + payload_len) {
        return NAV_STATUS_BAD_FRAME;
    }

    out->type = (nav_radio_message_type_t)bytes[2];
    out->frame_seq = (uint16_t)bytes[3] | ((uint16_t)bytes[4] << 8);
    out->payload = &bytes[NAV_RADIO_HEADER_LEN];
    out->payload_len = payload_len;
    return NAV_STATUS_OK;
}

nav_status_t nav_radio_encode_frame(const nav_radio_frame_t *frame, uint8_t *out, size_t out_capacity, size_t *out_len)
{
    if (frame == NULL || out == NULL || out_len == NULL) {
        return NAV_STATUS_INVALID_ARGUMENT;
    }
    if (frame->payload_len > NAV_RADIO_MAX_FRAME_BYTES || out_capacity < NAV_RADIO_HEADER_LEN + frame->payload_len) {
        return NAV_STATUS_BAD_FRAME;
    }
    out[0] = NAV_RADIO_MAGIC0;
    out[1] = NAV_RADIO_MAGIC1;
    out[2] = (uint8_t)frame->type;
    out[3] = (uint8_t)(frame->frame_seq & 0xFFu);
    out[4] = (uint8_t)(frame->frame_seq >> 8);
    out[5] = (uint8_t)frame->payload_len;
    for (size_t i = 0; i < frame->payload_len; ++i) {
        out[NAV_RADIO_HEADER_LEN + i] = frame->payload == NULL ? 0u : frame->payload[i];
    }
    *out_len = NAV_RADIO_HEADER_LEN + frame->payload_len;
    return NAV_STATUS_OK;
}

const char *nav_radio_message_type_to_string(nav_radio_message_type_t type)
{
    switch (type) {
    case NAV_RADIO_MSG_SET_NODE_ID:
        return "SET_NODE_ID";
    case NAV_RADIO_MSG_SET_CONFIG:
        return "SET_CONFIG";
    case NAV_RADIO_MSG_SET_SLOT_CONFIG:
        return "SET_SLOT_CONFIG";
    case NAV_RADIO_MSG_SEND_BEACON:
        return "SEND_BEACON";
    case NAV_RADIO_MSG_REQUEST_RANGE:
        return "REQUEST_RANGE";
    case NAV_RADIO_MSG_GET_STATUS:
        return "GET_STATUS";
    case NAV_RADIO_MSG_RESET:
        return "RESET";
    case NAV_RADIO_MSG_HEARTBEAT:
        return "HEARTBEAT";
    case NAV_RADIO_MSG_STATUS:
        return "STATUS";
    case NAV_RADIO_MSG_BEACON_RX:
        return "BEACON_RX";
    case NAV_RADIO_MSG_RANGE_RESULT:
        return "RANGE_RESULT";
    case NAV_RADIO_MSG_RANGE_FAIL:
        return "RANGE_FAIL";
    case NAV_RADIO_MSG_STATS:
        return "STATS";
    case NAV_RADIO_MSG_LOG_TEXT:
        return "LOG_TEXT";
    default:
        return "UNKNOWN";
    }
}
