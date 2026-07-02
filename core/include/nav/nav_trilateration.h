#ifndef NAV_TRILATERATION_H
#define NAV_TRILATERATION_H

#include "nav/nav_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NAV_TRILAT_OK = 0,
    NAV_TRILAT_ERR_NULL = -1,
    NAV_TRILAT_ERR_INVALID_INPUT = -2,
    NAV_TRILAT_ERR_POOR_GEOMETRY = -3,
    NAV_TRILAT_ERR_NO_CONVERGENCE = -4
} nav_trilat_status_t;

typedef struct {
    double lat_deg;
    double lon_deg;
    double alt_m;
    double distance_m;
    uint8_t node_id;
} nav_trilat_anchor_t;

typedef struct {
    double lat_deg;
    double lon_deg;
    double alt_m;
    double rms_error_m;
    double max_abs_error_m;
    double residuals_m[NAV_TRILAT_ANCHOR_COUNT];
    double expected_distances_m[NAV_TRILAT_ANCHOR_COUNT];
    int iterations;
    double geometry_condition;
} nav_trilat_result_t;

nav_trilat_status_t nav_trilat_solve_3_anchor_altitude(
    const nav_trilat_anchor_t anchors[NAV_TRILAT_ANCHOR_COUNT],
    double target_alt_m,
    nav_trilat_result_t *result
);

/* Straight-line (ECEF chord) distance in metres between two WGS84 geodetic
 * points. This is the same metric the solver minimises, so a range produced
 * from it is exactly consistent with the trilateration inputs. */
double nav_trilat_distance_m(
    double lat1_deg, double lon1_deg, double alt1_m,
    double lat2_deg, double lon2_deg, double alt2_m
);

const char *nav_trilat_status_to_string(nav_trilat_status_t status);

#ifdef __cplusplus
}
#endif

#endif
