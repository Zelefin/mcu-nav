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
        if (a.type == NAV_TDMA_TX_BEACON) {
            beacons++;
            assert(slot == 1u); /* node 1 owns telemetry slot 1 */
        } else if (a.type == NAV_TDMA_RANGE_PEER) {
            if (a.peer_id == 2u) range_to_2++;
            else if (a.peer_id == 3u) range_to_3++;
            else range_other++;
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

    return 0;
}
