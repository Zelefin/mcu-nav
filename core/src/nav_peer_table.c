#include "nav/nav_peer_table.h"

#include <string.h>

static bool node_id_valid(uint8_t node_id)
{
    return node_id < NAV_MAX_NODES;
}

static nav_peer_state_t *slot_for(nav_peer_table_t *table, uint8_t node_id)
{
    if (table == NULL || !node_id_valid(node_id)) {
        return NULL;
    }
    return &table->peers[node_id];
}

void nav_peer_table_init(nav_peer_table_t *table)
{
    if (table == NULL) {
        return;
    }
    memset(table, 0, sizeof(*table));
    for (uint8_t i = 0; i < NAV_MAX_NODES; ++i) {
        table->peers[i].node_id = i;
        table->peers[i].last_reject_reason = NAV_REJECT_NONE;
    }
}

nav_peer_state_t *nav_peer_table_get(nav_peer_table_t *table, uint8_t node_id)
{
    nav_peer_state_t *peer = slot_for(table, node_id);
    if (peer == NULL || !peer->present) {
        return NULL;
    }
    return peer;
}

const nav_peer_state_t *nav_peer_table_get_const(const nav_peer_table_t *table, uint8_t node_id)
{
    if (table == NULL || !node_id_valid(node_id)) {
        return NULL;
    }
    const nav_peer_state_t *peer = &table->peers[node_id];
    return peer->present ? peer : NULL;
}

bool nav_peer_table_update_telemetry(nav_peer_table_t *table, const nav_peer_telemetry_t *telemetry, uint32_t now_ms)
{
    if (telemetry == NULL) {
        return false;
    }
    nav_peer_state_t *peer = slot_for(table, telemetry->node_id);
    if (peer == NULL) {
        return false;
    }

    peer->node_id = telemetry->node_id;
    peer->present = true;
    peer->last_telemetry_timestamp_ms = now_ms;
    peer->packet_seq = telemetry->packet_seq;
    peer->position = telemetry->position;
    peer->velocity = telemetry->velocity;
    peer->fix_type = telemetry->fix_type;
    peer->gnss_valid = telemetry->gnss_valid;
    peer->satellites = telemetry->satellites;
    peer->hdop_centi = telemetry->hdop_centi;
    peer->hacc_mm = telemetry->hacc_mm;
    peer->vacc_mm = telemetry->vacc_mm;
    peer->peer_nav_mode = telemetry->nav_mode;
    peer->solution_status = telemetry->solution_status;
    peer->solution_source = telemetry->solution_source;
    peer->last_reject_reason = NAV_REJECT_NONE;
    return true;
}

bool nav_peer_table_update_beacon_rx(nav_peer_table_t *table, const nav_peer_beacon_rx_t *beacon, uint32_t now_ms)
{
    if (beacon == NULL) {
        return false;
    }
    if (!nav_peer_table_update_telemetry(table, &beacon->telemetry, now_ms)) {
        return false;
    }
    nav_peer_state_t *peer = nav_peer_table_get(table, beacon->telemetry.node_id);
    if (peer == NULL) {
        return false;
    }
    peer->rssi_dbm = beacon->rssi_dbm;
    peer->snr_db = beacon->snr_db;
    return true;
}

bool nav_peer_table_update_range(nav_peer_table_t *table, const nav_range_result_t *range, uint32_t now_ms)
{
    if (range == NULL) {
        return false;
    }
    nav_peer_state_t *peer = slot_for(table, range->peer_id);
    if (peer == NULL) {
        return false;
    }

    peer->node_id = range->peer_id;
    peer->present = true;
    peer->last_range_timestamp_ms = now_ms;
    peer->last_range_request_id = range->request_id;
    peer->range_mm = range->range_mm;
    peer->range_sigma_mm = range->range_sigma_mm;
    peer->range_valid = range->valid;
    peer->rssi_dbm = range->rssi_dbm;
    peer->snr_db = range->snr_db;
    peer->last_reject_reason = range->valid ? NAV_REJECT_NONE : NAV_REJECT_STALE_RANGE;
    return true;
}

void nav_peer_table_mark_stale(nav_peer_table_t *table, uint32_t now_ms, uint32_t telemetry_ttl_ms, uint32_t range_ttl_ms)
{
    if (table == NULL) {
        return;
    }
    for (uint8_t i = 0; i < NAV_MAX_NODES; ++i) {
        nav_peer_state_t *peer = &table->peers[i];
        if (!peer->present) {
            continue;
        }
        const uint32_t telemetry_age = now_ms - peer->last_telemetry_timestamp_ms;
        const uint32_t range_age = now_ms - peer->last_range_timestamp_ms;
        if (telemetry_ttl_ms > 0u && telemetry_age > telemetry_ttl_ms) {
            peer->gnss_valid = false;
            peer->telemetry_quality = 0.0f;
            peer->anchor_quality = 0.0f;
            peer->last_reject_reason = NAV_REJECT_STALE_TELEMETRY;
        }
        if (range_ttl_ms > 0u && range_age > range_ttl_ms) {
            peer->range_valid = false;
            peer->range_quality = 0.0f;
            peer->anchor_quality = 0.0f;
            if (peer->last_reject_reason == NAV_REJECT_NONE) {
                peer->last_reject_reason = NAV_REJECT_STALE_RANGE;
            }
        }
    }
}
