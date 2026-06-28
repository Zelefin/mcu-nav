#include <assert.h>
#include <string.h>

#include "nav/nav_quality.h"

int main(void)
{
    nav_peer_state_t peer = {
        .node_id = 1u,
        .present = true,
        .last_telemetry_timestamp_ms = 1000u,
        .last_range_timestamp_ms = 1100u,
        .fix_type = NAV_GNSS_FIX_3D,
        .gnss_valid = true,
        .range_mm = 500000u,
        .range_sigma_mm = 200u,
        .range_valid = true,
    };

    nav_anchor_quality_t q = nav_quality_score_anchor(&peer, 1200u, 1000u, 1000u, 10000u);
    assert(q.reject_reason == NAV_REJECT_NONE);
    assert(q.total_quality > 0.0f);
    assert(strcmp(nav_reject_reason_to_string(NAV_REJECT_BAD_GNSS), "BAD_GNSS") == 0);
    assert(strcmp(nav_solution_status_to_string(NAV_SOLUTION_RADIO_3D), "RADIO_3D") == 0);

    q = nav_quality_score_anchor(&peer, 2501u, 1000u, 1000u, 10000u);
    assert(q.reject_reason == NAV_REJECT_STALE_TELEMETRY);

    peer.last_telemetry_timestamp_ms = 2500u;
    peer.last_range_timestamp_ms = 2500u;
    peer.gnss_valid = false;
    q = nav_quality_score_anchor(&peer, 2501u, 1000u, 1000u, 10000u);
    assert(q.reject_reason == NAV_REJECT_BAD_GNSS);
    assert(strcmp(nav_reject_reason_to_string(NAV_REJECT_MISSING_LOCAL_ALTITUDE), "MISSING_LOCAL_ALTITUDE") == 0);
    assert(strcmp(nav_altitude_source_to_string(NAV_ALT_SOURCE_MANUAL), "MANUAL") == 0);
    return 0;
}
