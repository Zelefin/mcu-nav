#include "nav/nav_tdma.h"

#include <string.h>

void nav_tdma_init(nav_tdma_t *tdma, uint8_t local_node_id, uint32_t slot_ms)
{
    if (tdma == NULL) {
        return;
    }
    memset(tdma, 0, sizeof(*tdma));
    tdma->local_node_id = local_node_id;
    tdma->slot_ms = slot_ms == 0u ? 1u : slot_ms;
}

nav_status_t nav_tdma_set_members(nav_tdma_t *tdma, const uint8_t *ids, size_t count)
{
    if (tdma == NULL || (ids == NULL && count > 0u)) {
        return NAV_STATUS_INVALID_ARGUMENT;
    }
    if (count > NAV_MAX_NODES) {
        return NAV_STATUS_NO_SPACE;
    }
    for (size_t i = 0; i < count; ++i) {
        tdma->members[i] = ids[i];
    }
    /* Insertion sort ascending: tiny, stable, deterministic. */
    for (size_t i = 1; i < count; ++i) {
        const uint8_t key = tdma->members[i];
        size_t j = i;
        while (j > 0u && tdma->members[j - 1u] > key) {
            tdma->members[j] = tdma->members[j - 1u];
            j--;
        }
        tdma->members[j] = key;
    }
    tdma->member_count = count;
    return NAV_STATUS_OK;
}

nav_status_t nav_tdma_set_fixed_field_members(nav_tdma_t *tdma)
{
    static const uint8_t ids[NAV_MAX_NODES] = {0u, 1u, 2u, 3u};
    return nav_tdma_set_members(tdma, ids, NAV_MAX_NODES);
}

static size_t pair_count(size_t n)
{
    return n < 2u ? 0u : (n * (n - 1u)) / 2u;
}

size_t nav_tdma_slots_per_frame(const nav_tdma_t *tdma)
{
    if (tdma == NULL || tdma->member_count == 0u) {
        return 0u;
    }
    return tdma->member_count + pair_count(tdma->member_count);
}

uint32_t nav_tdma_frame_duration_ms(const nav_tdma_t *tdma)
{
    const size_t slots = nav_tdma_slots_per_frame(tdma);
    if (tdma == NULL || slots == 0u || slots > (UINT32_MAX / tdma->slot_ms)) {
        return 0u;
    }
    return (uint32_t)slots * tdma->slot_ms;
}

bool nav_tdma_fixed_plan_valid(const nav_tdma_t *tdma)
{
    if (tdma == NULL || tdma->member_count != NAV_MAX_NODES || tdma->slot_ms != NAV_TDMA_FIXED_SLOT_MS) {
        return false;
    }
    for (uint8_t i = 0u; i < NAV_MAX_NODES; ++i) {
        if (tdma->members[i] != i) {
            return false;
        }
    }
    return true;
}

bool nav_tdma_is_time_authority(const nav_tdma_t *tdma)
{
    if (tdma == NULL || tdma->member_count == 0u) {
        return false;
    }
    return tdma->members[0] == tdma->local_node_id;
}

/* Maps a ranging-slot ordinal to its (lower, higher) member pair, walking the
 * upper triangle of the member matrix in row-major order. */
static void ranging_pair(const nav_tdma_t *tdma, size_t ordinal, uint8_t *lo, uint8_t *hi)
{
    size_t seen = 0u;
    for (size_t i = 0; i < tdma->member_count; ++i) {
        for (size_t j = i + 1u; j < tdma->member_count; ++j) {
            if (seen == ordinal) {
                *lo = tdma->members[i];
                *hi = tdma->members[j];
                return;
            }
            seen++;
        }
    }
    *lo = NAV_INVALID_NODE_ID;
    *hi = NAV_INVALID_NODE_ID;
}

nav_status_t nav_tdma_action_at(const nav_tdma_t *tdma, uint32_t now_ms, nav_tdma_action_t *out)
{
    if (tdma == NULL || out == NULL) {
        return NAV_STATUS_INVALID_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));
    out->type = NAV_TDMA_LISTEN;
    out->peer_id = NAV_INVALID_NODE_ID;
    out->from_id = NAV_INVALID_NODE_ID;
    out->to_id = NAV_INVALID_NODE_ID;

    const size_t slots = nav_tdma_slots_per_frame(tdma);
    if (slots == 0u) {
        return NAV_STATUS_OK;
    }

    const uint32_t absolute_slot = now_ms / tdma->slot_ms;
    out->frame_index = absolute_slot / (uint32_t)slots;
    const size_t slot = (size_t)(absolute_slot % (uint32_t)slots);
    out->slot_index = (uint32_t)slot;
    out->slot_start_ms = absolute_slot * tdma->slot_ms;
    out->slot_end_ms = out->slot_start_ms + tdma->slot_ms;
    out->remaining_ms = out->slot_end_ms - now_ms;

    if (slot < tdma->member_count) {
        out->from_id = tdma->members[slot];
        if (tdma->members[slot] == tdma->local_node_id) {
            out->type = NAV_TDMA_TX_BEACON;
            out->peer_id = NAV_INVALID_NODE_ID;
        }
        return NAV_STATUS_OK;
    }

    uint8_t lo = NAV_INVALID_NODE_ID;
    uint8_t hi = NAV_INVALID_NODE_ID;
    ranging_pair(tdma, slot - tdma->member_count, &lo, &hi);
    out->from_id = lo;
    out->to_id = hi;
    if (tdma->local_node_id == lo) {
        out->type = NAV_TDMA_RANGE_PEER;
        out->peer_id = hi;
    } else if (tdma->local_node_id == hi) {
        out->peer_id = lo;
    }
    return NAV_STATUS_OK;
}

bool nav_tdma_action_can_start(const nav_tdma_action_t *action)
{
    if (action == NULL) {
        return false;
    }
    switch (action->type) {
    case NAV_TDMA_TX_BEACON:
        return action->remaining_ms >= NAV_TDMA_BEACON_MIN_REMAINING_MS;
    case NAV_TDMA_RANGE_PEER:
        return action->remaining_ms >= NAV_TDMA_RANGE_MIN_REMAINING_MS;
    case NAV_TDMA_LISTEN:
    default:
        return true;
    }
}

nav_status_t nav_tdma_validate_timing_heartbeat(
    const nav_tdma_t *tdma,
    const nav_tdma_timing_heartbeat_t *heartbeat
)
{
    if (tdma == NULL || heartbeat == NULL) {
        return NAV_STATUS_INVALID_ARGUMENT;
    }
    if (!nav_tdma_fixed_plan_valid(tdma)) {
        return NAV_STATUS_BAD_FRAME;
    }
    if (heartbeat->authority_id != NAV_TDMA_AUTHORITY_ID || heartbeat->slot_ms != tdma->slot_ms) {
        return NAV_STATUS_BAD_FRAME;
    }
    if (heartbeat->slot_index >= nav_tdma_slots_per_frame(tdma)) {
        return NAV_STATUS_BAD_FRAME;
    }
    if (heartbeat->slot_elapsed_ms >= heartbeat->slot_ms) {
        return NAV_STATUS_BAD_FRAME;
    }
    return NAV_STATUS_OK;
}

bool nav_tdma_authority_expired(uint32_t now_ms, uint32_t last_heartbeat_ms)
{
    return (uint32_t)(now_ms - last_heartbeat_ms) > NAV_TDMA_AUTHORITY_TIMEOUT_MS;
}
