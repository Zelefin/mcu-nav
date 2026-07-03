#include <assert.h>
#include <math.h>

#include "nav/nav_trilateration.h"

int main(void)
{
    const nav_trilat_anchor_t anchors[NAV_TRILAT_ANCHOR_COUNT] = {
        {.lat_deg = 50.4501, .lon_deg = 30.5234, .alt_m = 180.0, .distance_m = 394.1347351919918, .node_id = 0u},
        {.lat_deg = 50.4565, .lon_deg = 30.5201, .alt_m = 190.0, .distance_m = 621.9571290990954, .node_id = 1u},
        {.lat_deg = 50.4510, .lon_deg = 30.5340, .alt_m = 175.0, .distance_m = 553.3913539405108, .node_id = 2u},
    };

    nav_trilat_result_t result;
    const nav_trilat_status_t status = nav_trilat_solve_3_anchor_altitude(anchors, 183.5, &result);
    assert(status == NAV_TRILAT_OK);
    assert(fabs(result.lat_deg - 50.4529) < 1.0e-8);
    assert(fabs(result.lon_deg - 30.5268) < 1.0e-8);
    assert(fabs(result.alt_m - 183.5) < 1.0e-12);
    assert(result.rms_error_m < 1.0e-5);
    assert(result.geometry_condition > 1.0);
    assert(nav_trilat_solve_3_anchor_altitude(0, 183.5, &result) == NAV_TRILAT_ERR_NULL);

    const nav_trilat_anchor_t quantized_anchors[NAV_TRILAT_ANCHOR_COUNT] = {
        {.lat_deg = 50.4501, .lon_deg = 30.5234, .alt_m = 180.0, .distance_m = 394.135, .node_id = 0u},
        {.lat_deg = 50.4565, .lon_deg = 30.5201, .alt_m = 190.0, .distance_m = 621.957, .node_id = 1u},
        {.lat_deg = 50.4510, .lon_deg = 30.5340, .alt_m = 175.0, .distance_m = 553.391, .node_id = 2u},
    };
    assert(nav_trilat_solve_3_anchor_altitude(quantized_anchors, 183.5, &result) == NAV_TRILAT_OK);
    assert(fabs(result.lat_deg - 50.4529) < 1.0e-8);
    assert(fabs(result.lon_deg - 30.5268) < 1.0e-8);
    assert(result.rms_error_m < 0.001);
    return 0;
}
