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

    const size_t slots = nav_tdma_slots_per_frame(tdma);
    if (slots == 0u) {
        return NAV_STATUS_OK;
    }

    const uint32_t absolute_slot = now_ms / tdma->slot_ms;
    out->frame_index = absolute_slot / (uint32_t)slots;
    const size_t slot = (size_t)(absolute_slot % (uint32_t)slots);
    out->slot_index = (uint32_t)slot;

    if (slot < tdma->member_count) {
        if (tdma->members[slot] == tdma->local_node_id) {
            out->type = NAV_TDMA_TX_BEACON;
        }
        return NAV_STATUS_OK;
    }

    uint8_t lo = NAV_INVALID_NODE_ID;
    uint8_t hi = NAV_INVALID_NODE_ID;
    ranging_pair(tdma, slot - tdma->member_count, &lo, &hi);
    if (tdma->local_node_id == lo) {
        out->type = NAV_TDMA_RANGE_PEER;
        out->peer_id = hi;
    }
    return NAV_STATUS_OK;
}
