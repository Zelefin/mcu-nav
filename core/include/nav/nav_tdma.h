#ifndef NAV_TDMA_H
#define NAV_TDMA_H

#include <stddef.h>

#include "nav/nav_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Single-hop TDMA slot scheduler (see CONTEXT.md).
 *
 * A frame is a deterministic sequence of fixed-length slots: first one
 * telemetry slot per member (the member beacons; everyone else listens), then
 * one ranging slot per unique member pair (the lower-id member initiates the
 * two-way range; the other listens). Given the wall clock, the scheduler tells a
 * node what to do in the current slot. Pure and host-testable; the port layer
 * drives the radio from the returned action. The lowest member id is the TDMA
 * time authority whose frame timing the others follow. */

typedef enum {
    NAV_TDMA_LISTEN = 0,    /* receive (peer beacon, or be ranged by a peer) */
    NAV_TDMA_TX_BEACON,     /* transmit this node's own telemetry beacon */
    NAV_TDMA_RANGE_PEER     /* initiate a ranging exchange with peer_id */
} nav_tdma_action_type_t;

#define NAV_TDMA_AUTHORITY_ID 0u
#define NAV_TDMA_FIXED_SLOT_MS 500u
#define NAV_TDMA_AUTHORITY_TIMEOUT_MS 15000u
#define NAV_TDMA_RANGE_MIN_REMAINING_MS 400u
#define NAV_TDMA_BEACON_MIN_REMAINING_MS 100u

typedef struct {
    nav_tdma_action_type_t type;
    uint8_t peer_id;       /* target peer for NAV_TDMA_RANGE_PEER */
    uint8_t from_id;       /* scheduled ranging master, or beacon sender */
    uint8_t to_id;         /* scheduled ranging slave, or NAV_INVALID_NODE_ID */
    uint32_t slot_index;   /* slot within the current frame */
    uint32_t frame_index;  /* frame number since epoch */
    uint32_t slot_start_ms;
    uint32_t slot_end_ms;
    uint32_t remaining_ms;
} nav_tdma_action_t;

typedef struct {
    uint8_t authority_id;
    uint32_t frame_index;
    uint32_t slot_index;
    uint16_t slot_ms;
} nav_tdma_timing_heartbeat_t;

typedef struct {
    uint8_t members[NAV_MAX_NODES]; /* sorted ascending */
    size_t member_count;
    uint8_t local_node_id;
    uint32_t slot_ms;
} nav_tdma_t;

void nav_tdma_init(nav_tdma_t *tdma, uint8_t local_node_id, uint32_t slot_ms);

/* Sets the network membership (ids are copied and sorted ascending). Returns
 * NAV_STATUS_NO_SPACE if count exceeds NAV_MAX_NODES. */
nav_status_t nav_tdma_set_members(nav_tdma_t *tdma, const uint8_t *ids, size_t count);

/* Sets the first field-test membership: fixed node ids 0, 1, 2, 3. */
nav_status_t nav_tdma_set_fixed_field_members(nav_tdma_t *tdma);

/* Total slots in one frame: member_count telemetry slots plus one slot per
 * unique member pair. */
size_t nav_tdma_slots_per_frame(const nav_tdma_t *tdma);

uint32_t nav_tdma_frame_duration_ms(const nav_tdma_t *tdma);

bool nav_tdma_fixed_plan_valid(const nav_tdma_t *tdma);

/* True if the local node is the TDMA time authority (lowest member id). */
bool nav_tdma_is_time_authority(const nav_tdma_t *tdma);

/* Resolves the action for the slot active at now_ms. */
nav_status_t nav_tdma_action_at(const nav_tdma_t *tdma, uint32_t now_ms, nav_tdma_action_t *out);

bool nav_tdma_action_can_start(const nav_tdma_action_t *action);

nav_status_t nav_tdma_validate_timing_heartbeat(
    const nav_tdma_t *tdma,
    const nav_tdma_timing_heartbeat_t *heartbeat
);

bool nav_tdma_authority_expired(uint32_t now_ms, uint32_t last_heartbeat_ms);

#ifdef __cplusplus
}
#endif

#endif
