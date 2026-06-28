#ifndef NAV_QUALITY_H
#define NAV_QUALITY_H

#include "nav/nav_peer_table.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float gnss_quality;
    float telemetry_quality;
    float range_quality;
    float geometry_quality;
    float total_quality;
    nav_reject_reason_t reject_reason;
} nav_anchor_quality_t;

nav_anchor_quality_t nav_quality_score_anchor(
    const nav_peer_state_t *peer,
    uint32_t now_ms,
    uint32_t telemetry_ttl_ms,
    uint32_t range_ttl_ms,
    uint32_t max_range_sigma_mm
);

const char *nav_reject_reason_to_string(nav_reject_reason_t reason);
const char *nav_solution_status_to_string(nav_solution_status_t status);
const char *nav_solution_source_to_string(nav_solution_source_t source);
const char *nav_altitude_source_to_string(nav_altitude_source_t source);
const char *nav_range_fail_reason_to_string(nav_range_fail_reason_t reason);

#ifdef __cplusplus
}
#endif

#endif
