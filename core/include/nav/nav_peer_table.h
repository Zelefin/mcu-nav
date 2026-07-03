#ifndef NAV_PEER_TABLE_H
#define NAV_PEER_TABLE_H

#include "nav/nav_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t node_id;
    bool present;
    uint32_t last_telemetry_timestamp_ms;
    uint32_t last_range_timestamp_ms;
    uint32_t packet_seq;
    uint16_t last_range_request_id;
    nav_position_t position;
    nav_position_t range_position;
    uint32_t range_position_timestamp_ms;
    nav_velocity_t velocity;
    nav_gnss_fix_type_t fix_type;
    bool gnss_valid;
    uint8_t satellites;
    uint16_t hdop_centi;
    uint32_t hacc_mm;
    uint32_t vacc_mm;
    nav_mode_t peer_nav_mode;
    uint32_t range_mm;
    uint32_t range_sigma_mm;
    bool range_valid;
    bool range_position_valid;
    int16_t rssi_dbm;
    int16_t snr_db;
    float packet_loss_estimate;
    float telemetry_quality;
    float range_quality;
    float anchor_quality;
    float last_residual_m;
    nav_reject_reason_t last_reject_reason;
} nav_peer_state_t;

typedef struct {
    nav_peer_state_t peers[NAV_MAX_NODES];
} nav_peer_table_t;

void nav_peer_table_init(nav_peer_table_t *table);
nav_peer_state_t *nav_peer_table_get(nav_peer_table_t *table, uint8_t node_id);
const nav_peer_state_t *nav_peer_table_get_const(const nav_peer_table_t *table, uint8_t node_id);
bool nav_peer_table_update_telemetry(nav_peer_table_t *table, const nav_peer_telemetry_t *telemetry, uint32_t now_ms);
bool nav_peer_table_update_beacon_rx(nav_peer_table_t *table, const nav_peer_beacon_rx_t *beacon, uint32_t now_ms);
bool nav_peer_table_update_range(nav_peer_table_t *table, const nav_range_result_t *range, uint32_t now_ms);
void nav_peer_table_mark_stale(nav_peer_table_t *table, uint32_t now_ms, uint32_t telemetry_ttl_ms, uint32_t range_ttl_ms);

#ifdef __cplusplus
}
#endif

#endif
