#ifndef NAV_RADIO_PROTOCOL_H
#define NAV_RADIO_PROTOCOL_H

#include <stddef.h>

#include "nav/nav_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NAV_RADIO_MAX_FRAME_BYTES 128u
#define NAV_RADIO_PROTOCOL_VERSION 0u

typedef enum {
    NAV_RADIO_MSG_SET_NODE_ID = 1,
    NAV_RADIO_MSG_SET_CONFIG = 2,
    NAV_RADIO_MSG_SET_SLOT_CONFIG = 3,
    NAV_RADIO_MSG_SEND_BEACON = 4,
    NAV_RADIO_MSG_REQUEST_RANGE = 5,
    NAV_RADIO_MSG_GET_STATUS = 6,
    NAV_RADIO_MSG_RESET = 7,
    NAV_RADIO_MSG_HEARTBEAT = 64,
    NAV_RADIO_MSG_STATUS = 65,
    NAV_RADIO_MSG_BEACON_RX = 66,
    NAV_RADIO_MSG_RANGE_RESULT = 67,
    NAV_RADIO_MSG_RANGE_FAIL = 68,
    NAV_RADIO_MSG_STATS = 69,
    NAV_RADIO_MSG_LOG_TEXT = 70,
    NAV_RADIO_MSG_DEBUG_ENABLE = 71,
    NAV_RADIO_MSG_NODE_QUALITY_REPORT = 72
} nav_radio_message_type_t;

typedef struct {
    nav_radio_message_type_t type;
    uint16_t frame_seq;
    const uint8_t *payload;
    size_t payload_len;
} nav_radio_frame_t;

typedef struct {
    uint8_t peer_id_u8;
    uint32_t packet_seq_u32;
    uint32_t radio_timestamp_ms_u32;
    int32_t lat_e7_i32;
    int32_t lon_e7_i32;
    int32_t alt_mm_i32;
    int32_t vel_n_mmps_i32;
    int32_t vel_e_mmps_i32;
    int32_t vel_d_mmps_i32;
    uint8_t gnss_fix_type_u8;
    uint8_t gnss_valid_u8;
    uint8_t nav_mode_u8;
    uint8_t solution_status_u8;
    uint8_t solution_source_u8;
    int16_t rssi_dbm_i16;
    int16_t snr_db_i16;
    uint8_t reserved_u8;
} nav_radio_beacon_rx_payload_t;

typedef struct {
    uint8_t peer_id_u8;
    uint8_t range_status_u8;
    uint32_t radio_timestamp_ms_u32;
    uint32_t range_mm_u32;
    uint32_t range_sigma_mm_u32;
    int16_t rssi_dbm_i16;
    int16_t snr_db_i16;
    uint8_t attempt_count_u8;
    uint16_t request_id_u16;
} nav_radio_range_result_payload_t;

typedef struct {
    uint8_t peer_id_u8;
    uint8_t range_fail_reason_u8;
    uint32_t radio_timestamp_ms_u32;
    uint8_t attempt_count_u8;
    int16_t last_rssi_dbm_i16;
    int16_t last_snr_db_i16;
    uint16_t request_id_u16;
} nav_radio_range_fail_payload_t;

typedef struct {
    uint8_t node_id_u8;
    uint8_t radio_mode_u8;
    uint32_t radio_fw_version_u32;
    uint32_t uptime_ms_u32;
    uint16_t last_error_u16;
    uint32_t capability_flags_u32;
} nav_radio_status_payload_t;

typedef struct {
    uint8_t node_id_u8;
    uint32_t uptime_ms_u32;
    uint32_t status_flags_u32;
    uint32_t tdma_frame_index_u32;
    uint8_t tdma_slot_index_u8;
    uint16_t tdma_slot_ms_u16;
    uint16_t tdma_slot_elapsed_ms_u16;
} nav_radio_heartbeat_payload_t;

typedef struct {
    uint8_t node_id_u8;
} nav_radio_set_node_id_payload_t;

typedef struct {
    uint8_t peer_id_u8;
    uint16_t request_id_u16;
    uint16_t timeout_ms_u16;
} nav_radio_request_range_payload_t;

typedef struct {
    uint8_t origin_node_id_u8;
    uint16_t ttl_ms_u16;
} nav_radio_debug_enable_payload_t;

typedef struct {
    uint8_t node_id_u8;
    uint8_t nav_mode_u8;
    uint8_t solution_status_u8;
    uint8_t solution_source_u8;
    uint8_t num_anchors_u8;
    uint8_t anchor_ids_u8[NAV_TRILAT_ANCHOR_COUNT];
    uint8_t fix_type_u8;
    uint8_t satellites_u8;
    uint8_t geometry_score_u8;
    uint8_t total_quality_u8;
    uint16_t residual_rms_mm_u16;
    uint16_t max_residual_mm_u16;
    uint16_t hdop_centi_u16;
    uint32_t hacc_mm_u32;
    uint32_t vacc_mm_u32;
    int32_t lat_e7_i32;
    int32_t lon_e7_i32;
    int32_t alt_mm_i32;
    uint32_t packet_seq_u32;
} nav_radio_node_quality_report_payload_t;

nav_status_t nav_radio_decode_frame(const uint8_t *bytes, size_t len, nav_radio_frame_t *out);
nav_status_t nav_radio_encode_frame(const nav_radio_frame_t *frame, uint8_t *out, size_t out_capacity, size_t *out_len);
const char *nav_radio_message_type_to_string(nav_radio_message_type_t type);

#ifdef __cplusplus
}
#endif

#endif
