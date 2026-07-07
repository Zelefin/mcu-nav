#include <assert.h>

#include "nav/nav_tdma.h"

int main(void)
{
    nav_tdma_t tdma;
    nav_tdma_init(&tdma, /*local=*/1u, /*slot_ms=*/10u);

    const uint8_t ids[4] = {3u, 1u, 2u, 0u};
    assert(nav_tdma_set_members(&tdma, ids, 4u) == NAV_STATUS_OK);
    /* sorted ascending */
    assert(tdma.members[0] == 0u && tdma.members[3] == 3u);
    assert(nav_tdma_slots_per_frame(&tdma) == 10u); /* 4 telemetry + 6 pairs */
    assert(!nav_tdma_is_time_authority(&tdma));     /* lowest id is 0, not 1 */

    int beacons = 0;
    int range_to_2 = 0, range_to_3 = 0, range_other = 0;
    nav_tdma_action_t a;
    for (uint32_t slot = 0; slot < 10u; ++slot) {
        assert(nav_tdma_action_at(&tdma, slot * 10u, &a) == NAV_STATUS_OK);
        assert(a.slot_index == slot);
        assert(a.frame_index == 0u);
        assert(a.slot_start_ms == slot * 10u);
        assert(a.slot_end_ms == (slot + 1u) * 10u);
        assert(a.remaining_ms == 10u);
        if (a.type == NAV_TDMA_TX_BEACON) {
            beacons++;
            assert(slot == 1u); /* node 1 owns telemetry slot 1 */
            assert(a.from_id == 1u && a.to_id == NAV_INVALID_NODE_ID);
        } else if (a.type == NAV_TDMA_RANGE_PEER) {
            assert(a.from_id == 1u);
            assert(a.to_id == a.peer_id);
            if (a.peer_id == 2u) range_to_2++;
            else if (a.peer_id == 3u) range_to_3++;
            else range_other++;
        } else if (slot >= 4u) {
            assert(a.from_id != NAV_INVALID_NODE_ID);
            assert(a.to_id != NAV_INVALID_NODE_ID);
        }
    }
    /* local node beacons exactly once and initiates ranging to each higher peer. */
    assert(beacons == 1);
    assert(range_to_2 == 1 && range_to_3 == 1 && range_other == 0);

    /* frame wraps deterministically. */
    assert(nav_tdma_action_at(&tdma, 100u, &a) == NAV_STATUS_OK);
    assert(a.frame_index == 1u && a.slot_index == 0u);

    /* time authority is the lowest member id. */
    nav_tdma_t auth;
    nav_tdma_init(&auth, 0u, 10u);
    assert(nav_tdma_set_members(&auth, ids, 4u) == NAV_STATUS_OK);
    assert(nav_tdma_is_time_authority(&auth));

    /* single member: telemetry only, no ranging slots. */
    nav_tdma_t solo;
    nav_tdma_init(&solo, 5u, 10u);
    const uint8_t one[1] = {5u};
    assert(nav_tdma_set_members(&solo, one, 1u) == NAV_STATUS_OK);
    assert(nav_tdma_slots_per_frame(&solo) == 1u);
    assert(nav_tdma_action_at(&solo, 0u, &a) == NAV_STATUS_OK);
    assert(a.type == NAV_TDMA_TX_BEACON);

    /* First field TDMA plan: fixed nodes 0..3, 500 ms slots, node 0 authority. */
    nav_tdma_t fixed;
    nav_tdma_init(&fixed, 2u, NAV_TDMA_FIXED_SLOT_MS);
    assert(nav_tdma_set_fixed_field_members(&fixed) == NAV_STATUS_OK);
    assert(nav_tdma_slots_per_frame(&fixed) == 10u);
    assert(nav_tdma_frame_duration_ms(&fixed) == 5000u);
    assert(nav_tdma_fixed_plan_valid(&fixed));

    assert(nav_tdma_action_at(&fixed, 0u, &a) == NAV_STATUS_OK);
    assert(a.type == NAV_TDMA_LISTEN);
    assert(a.from_id == 0u && a.to_id == NAV_INVALID_NODE_ID);
    assert(a.remaining_ms == 500u);

    assert(nav_tdma_action_at(&fixed, 2750u, &a) == NAV_STATUS_OK);
    assert(a.slot_index == 5u);
    assert(a.from_id == 0u && a.to_id == 2u);
    assert(a.type == NAV_TDMA_LISTEN); /* node 2 is the scheduled ranging slave */
    assert(a.remaining_ms == 250u);

    assert(nav_tdma_action_at(&fixed, 6250u, &a) == NAV_STATUS_OK);
    assert(a.frame_index == 1u);
    assert(a.slot_index == 2u);
    assert(a.type == NAV_TDMA_TX_BEACON);
    assert(a.remaining_ms == 250u);

    assert(nav_tdma_action_can_start(&a));
    assert(nav_tdma_action_at(&fixed, 6499u, &a) == NAV_STATUS_OK);
    assert(a.type == NAV_TDMA_TX_BEACON);
    assert(a.remaining_ms == 1u);
    assert(!nav_tdma_action_can_start(&a));

    nav_tdma_init(&fixed, 0u, NAV_TDMA_FIXED_SLOT_MS);
    assert(nav_tdma_set_fixed_field_members(&fixed) == NAV_STATUS_OK);
    assert(nav_tdma_action_at(&fixed, 2250u, &a) == NAV_STATUS_OK);
    assert(a.type == NAV_TDMA_RANGE_PEER);
    assert(a.from_id == 0u && a.to_id == 1u);
    assert(!nav_tdma_action_can_start(&a)); /* 250 ms left is below the range guard */

    assert(nav_tdma_action_at(&fixed, 2050u, &a) == NAV_STATUS_OK);
    assert(a.type == NAV_TDMA_RANGE_PEER);
    assert(nav_tdma_action_can_start(&a));

    nav_tdma_timing_heartbeat_t hb = {
        .authority_id = NAV_TDMA_AUTHORITY_ID,
        .frame_index = 3u,
        .slot_index = 2u,
        .slot_ms = NAV_TDMA_FIXED_SLOT_MS,
        .slot_elapsed_ms = 25u,
    };
    assert(nav_tdma_validate_timing_heartbeat(&fixed, &hb) == NAV_STATUS_OK);
    hb.authority_id = 1u;
    assert(nav_tdma_validate_timing_heartbeat(&fixed, &hb) == NAV_STATUS_BAD_FRAME);
    hb.authority_id = NAV_TDMA_AUTHORITY_ID;
    hb.slot_ms = 250u;
    assert(nav_tdma_validate_timing_heartbeat(&fixed, &hb) == NAV_STATUS_BAD_FRAME);
    hb.slot_ms = NAV_TDMA_FIXED_SLOT_MS;
    hb.slot_index = 10u;
    assert(nav_tdma_validate_timing_heartbeat(&fixed, &hb) == NAV_STATUS_BAD_FRAME);
    hb.slot_index = 2u;
    hb.slot_elapsed_ms = NAV_TDMA_FIXED_SLOT_MS;
    assert(nav_tdma_validate_timing_heartbeat(&fixed, &hb) == NAV_STATUS_BAD_FRAME);

    assert(!nav_tdma_authority_expired(20000u, 5000u));
    assert(nav_tdma_authority_expired(20001u, 5000u));

    return 0;
}
