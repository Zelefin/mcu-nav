#ifndef NAV_CONFIG_H
#define NAV_CONFIG_H

#include "nav/nav_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t local_node_id;
    uint32_t telemetry_ttl_ms;
    uint32_t range_ttl_ms;
    uint32_t local_altitude_ttl_ms;
    uint32_t tick_period_ms;
    uint32_t max_range_sigma_mm;
    float min_anchor_quality;
    float min_solution_quality;
    float max_residual_rms_m;
    float max_residual_m;
    float min_anchor_triangle_area_m2;
    float degraded_anchor_triangle_area_m2;
    bool demo_force_gps_denied;
    bool allow_gnss_altitude_in_demo_forced_denied;
} nav_config_t;

nav_config_t nav_config_default(uint8_t local_node_id);

#ifdef __cplusplus
}
#endif

#endif
