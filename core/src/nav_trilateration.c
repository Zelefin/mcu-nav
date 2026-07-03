#include "nav/nav_trilateration.h"

#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define WGS84_A_M 6378137.0
#define WGS84_F (1.0 / 298.257223563)
#define WGS84_B_M (WGS84_A_M * (1.0 - WGS84_F))
#define WGS84_E2 (WGS84_F * (2.0 - WGS84_F))
#define WGS84_EP2 (((WGS84_A_M * WGS84_A_M) - (WGS84_B_M * WGS84_B_M)) / (WGS84_B_M * WGS84_B_M))

#define NAV_TRILAT_PI 3.14159265358979323846264338327950288
#define DEG_TO_RAD (NAV_TRILAT_PI / 180.0)
#define RAD_TO_DEG (180.0 / NAV_TRILAT_PI)
#define MAX_GEOMETRY_CONDITION 1.0e8
#define MAX_ITERATIONS 30
#define FINITE_DIFF_RAD 1.0e-8
#define CONVERGENCE_STEP_RAD 1.0e-14

typedef struct {
    double x;
    double y;
    double z;
} nav_vec3_t;

static bool is_valid_lat_lon(double lat_deg, double lon_deg)
{
    return isfinite(lat_deg) && isfinite(lon_deg) && lat_deg >= -90.0 && lat_deg <= 90.0 &&
           lon_deg >= -180.0 && lon_deg <= 180.0;
}

static double normalize_lon_deg(double lon_deg)
{
    double lon = fmod(lon_deg + 180.0, 360.0);
    if (lon < 0.0) {
        lon += 360.0;
    }
    return lon - 180.0;
}

static nav_vec3_t vec_sub(nav_vec3_t a, nav_vec3_t b)
{
    nav_vec3_t out = {a.x - b.x, a.y - b.y, a.z - b.z};
    return out;
}

static double vec_norm(nav_vec3_t v)
{
    return sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

static nav_vec3_t geodetic_to_ecef(double lat_deg, double lon_deg, double alt_m)
{
    const double lat = lat_deg * DEG_TO_RAD;
    const double lon = lon_deg * DEG_TO_RAD;
    const double sin_lat = sin(lat);
    const double cos_lat = cos(lat);
    const double sin_lon = sin(lon);
    const double cos_lon = cos(lon);
    const double n = WGS84_A_M / sqrt(1.0 - WGS84_E2 * sin_lat * sin_lat);

    nav_vec3_t out = {
        (n + alt_m) * cos_lat * cos_lon,
        (n + alt_m) * cos_lat * sin_lon,
        (n * (1.0 - WGS84_E2) + alt_m) * sin_lat,
    };
    return out;
}

static void ecef_to_geodetic(nav_vec3_t ecef_m, double *lat_deg, double *lon_deg, double *alt_m)
{
    const double p = hypot(ecef_m.x, ecef_m.y);
    if (p == 0.0) {
        *lat_deg = ecef_m.z >= 0.0 ? 90.0 : -90.0;
        *lon_deg = 0.0;
        *alt_m = fabs(ecef_m.z) - WGS84_B_M;
        return;
    }

    const double theta = atan2(ecef_m.z * WGS84_A_M, p * WGS84_B_M);
    const double sin_theta = sin(theta);
    const double cos_theta = cos(theta);
    const double lat = atan2(
        ecef_m.z + WGS84_EP2 * WGS84_B_M * sin_theta * sin_theta * sin_theta,
        p - WGS84_E2 * WGS84_A_M * cos_theta * cos_theta * cos_theta
    );
    const double lon = atan2(ecef_m.y, ecef_m.x);
    const double sin_lat = sin(lat);
    const double n = WGS84_A_M / sqrt(1.0 - WGS84_E2 * sin_lat * sin_lat);

    *lat_deg = lat * RAD_TO_DEG;
    *lon_deg = normalize_lon_deg(lon * RAD_TO_DEG);
    *alt_m = p / cos(lat) - n;
}

static void ecef_to_enu_matrix(double ref_lat_deg, double ref_lon_deg, double rot[3][3])
{
    const double lat = ref_lat_deg * DEG_TO_RAD;
    const double lon = ref_lon_deg * DEG_TO_RAD;
    const double sin_lat = sin(lat);
    const double cos_lat = cos(lat);
    const double sin_lon = sin(lon);
    const double cos_lon = cos(lon);

    rot[0][0] = -sin_lon;
    rot[0][1] = cos_lon;
    rot[0][2] = 0.0;
    rot[1][0] = -sin_lat * cos_lon;
    rot[1][1] = -sin_lat * sin_lon;
    rot[1][2] = cos_lat;
    rot[2][0] = cos_lat * cos_lon;
    rot[2][1] = cos_lat * sin_lon;
    rot[2][2] = sin_lat;
}

static nav_vec3_t apply_rot(double rot[3][3], nav_vec3_t v)
{
    nav_vec3_t out = {
        rot[0][0] * v.x + rot[0][1] * v.y + rot[0][2] * v.z,
        rot[1][0] * v.x + rot[1][1] * v.y + rot[1][2] * v.z,
        rot[2][0] * v.x + rot[2][1] * v.y + rot[2][2] * v.z,
    };
    return out;
}

static nav_vec3_t enu_to_ecef(nav_vec3_t enu_m, double ref_lat_deg, double ref_lon_deg, double ref_alt_m)
{
    double rot[3][3];
    ecef_to_enu_matrix(ref_lat_deg, ref_lon_deg, rot);
    const nav_vec3_t ref_ecef = geodetic_to_ecef(ref_lat_deg, ref_lon_deg, ref_alt_m);

    nav_vec3_t delta = {
        rot[0][0] * enu_m.x + rot[1][0] * enu_m.y + rot[2][0] * enu_m.z,
        rot[0][1] * enu_m.x + rot[1][1] * enu_m.y + rot[2][1] * enu_m.z,
        rot[0][2] * enu_m.x + rot[1][2] * enu_m.y + rot[2][2] * enu_m.z,
    };
    nav_vec3_t out = {ref_ecef.x + delta.x, ref_ecef.y + delta.y, ref_ecef.z + delta.z};
    return out;
}

static bool solve_2x2(double a00, double a01, double a10, double a11, double b0, double b1, double *x0, double *x1)
{
    const double det = a00 * a11 - a01 * a10;
    if (!isfinite(det) || fabs(det) < 1.0e-18) {
        return false;
    }
    *x0 = (b0 * a11 - a01 * b1) / det;
    *x1 = (a00 * b1 - b0 * a10) / det;
    return isfinite(*x0) && isfinite(*x1);
}

static double geometry_condition(const nav_trilat_anchor_t anchors[NAV_TRILAT_ANCHOR_COUNT])
{
    double ref_lat = 0.0;
    double ref_lon = 0.0;
    double ref_alt = 0.0;
    for (int i = 0; i < NAV_TRILAT_ANCHOR_COUNT; ++i) {
        ref_lat += anchors[i].lat_deg / NAV_TRILAT_ANCHOR_COUNT;
        ref_lon += anchors[i].lon_deg / NAV_TRILAT_ANCHOR_COUNT;
        ref_alt += anchors[i].alt_m / NAV_TRILAT_ANCHOR_COUNT;
    }

    const nav_vec3_t ref_ecef = geodetic_to_ecef(ref_lat, ref_lon, ref_alt);
    double rot[3][3];
    ecef_to_enu_matrix(ref_lat, ref_lon, rot);

    nav_vec3_t points[NAV_TRILAT_ANCHOR_COUNT];
    double mean_x = 0.0;
    double mean_y = 0.0;
    for (int i = 0; i < NAV_TRILAT_ANCHOR_COUNT; ++i) {
        points[i] = apply_rot(rot, vec_sub(geodetic_to_ecef(anchors[i].lat_deg, anchors[i].lon_deg, anchors[i].alt_m), ref_ecef));
        mean_x += points[i].x / NAV_TRILAT_ANCHOR_COUNT;
        mean_y += points[i].y / NAV_TRILAT_ANCHOR_COUNT;
    }

    double sxx = 0.0;
    double sxy = 0.0;
    double syy = 0.0;
    for (int i = 0; i < NAV_TRILAT_ANCHOR_COUNT; ++i) {
        const double dx = points[i].x - mean_x;
        const double dy = points[i].y - mean_y;
        sxx += dx * dx;
        sxy += dx * dy;
        syy += dy * dy;
    }

    const double trace = sxx + syy;
    const double disc = sqrt(fmax(0.0, (sxx - syy) * (sxx - syy) + 4.0 * sxy * sxy));
    const double lambda_min = 0.5 * (trace - disc);
    const double lambda_max = 0.5 * (trace + disc);
    if (lambda_min < 1.0e-12) {
        return INFINITY;
    }
    return sqrt(lambda_max / lambda_min);
}

static bool initial_guess(
    const nav_trilat_anchor_t anchors[NAV_TRILAT_ANCHOR_COUNT],
    double target_alt_m,
    double *lat_deg,
    double *lon_deg
)
{
    double ref_lat = 0.0;
    double ref_lon = 0.0;
    for (int i = 0; i < NAV_TRILAT_ANCHOR_COUNT; ++i) {
        ref_lat += anchors[i].lat_deg / NAV_TRILAT_ANCHOR_COUNT;
        ref_lon += anchors[i].lon_deg / NAV_TRILAT_ANCHOR_COUNT;
    }

    const nav_vec3_t ref_ecef = geodetic_to_ecef(ref_lat, ref_lon, target_alt_m);
    double rot[3][3];
    ecef_to_enu_matrix(ref_lat, ref_lon, rot);

    nav_vec3_t anchor_enu[NAV_TRILAT_ANCHOR_COUNT];
    for (int i = 0; i < NAV_TRILAT_ANCHOR_COUNT; ++i) {
        anchor_enu[i] = apply_rot(rot, vec_sub(geodetic_to_ecef(anchors[i].lat_deg, anchors[i].lon_deg, anchors[i].alt_m), ref_ecef));
    }

    const nav_vec3_t first = anchor_enu[0];
    const double first_range_sq = anchors[0].distance_m * anchors[0].distance_m - first.z * first.z;

    double a00 = 0.0;
    double a01 = 0.0;
    double a10 = 0.0;
    double a11 = 0.0;
    double b0 = 0.0;
    double b1 = 0.0;
    for (int i = 1; i < NAV_TRILAT_ANCHOR_COUNT; ++i) {
        const nav_vec3_t point = anchor_enu[i];
        const double range_sq = anchors[i].distance_m * anchors[i].distance_m - point.z * point.z;
        const double row0 = 2.0 * (point.x - first.x);
        const double row1 = 2.0 * (point.y - first.y);
        const double rhs = first_range_sq - range_sq + point.x * point.x + point.y * point.y - first.x * first.x - first.y * first.y;

        a00 += row0 * row0;
        a01 += row0 * row1;
        a10 += row1 * row0;
        a11 += row1 * row1;
        b0 += row0 * rhs;
        b1 += row1 * rhs;
    }

    double local_x = 0.0;
    double local_y = 0.0;
    if (!solve_2x2(a00, a01, a10, a11, b0, b1, &local_x, &local_y)) {
        *lat_deg = ref_lat;
        *lon_deg = ref_lon;
        return false;
    }

    const nav_vec3_t candidate_ecef = enu_to_ecef((nav_vec3_t){local_x, local_y, 0.0}, ref_lat, ref_lon, target_alt_m);
    double ignored_alt = 0.0;
    ecef_to_geodetic(candidate_ecef, lat_deg, lon_deg, &ignored_alt);
    return true;
}

static void compute_residuals(
    const nav_trilat_anchor_t anchors[NAV_TRILAT_ANCHOR_COUNT],
    double lat_deg,
    double lon_deg,
    double alt_m,
    double residuals[NAV_TRILAT_ANCHOR_COUNT],
    double expected_distances[NAV_TRILAT_ANCHOR_COUNT],
    double *rms_error_m,
    double *max_abs_error_m
)
{
    const nav_vec3_t target = geodetic_to_ecef(lat_deg, lon_deg, alt_m);
    double sum_sq = 0.0;
    double max_abs = 0.0;
    for (int i = 0; i < NAV_TRILAT_ANCHOR_COUNT; ++i) {
        const nav_vec3_t anchor = geodetic_to_ecef(anchors[i].lat_deg, anchors[i].lon_deg, anchors[i].alt_m);
        expected_distances[i] = vec_norm(vec_sub(anchor, target));
        residuals[i] = expected_distances[i] - anchors[i].distance_m;
        sum_sq += residuals[i] * residuals[i];
        max_abs = fmax(max_abs, fabs(residuals[i]));
    }
    *rms_error_m = sqrt(sum_sq / NAV_TRILAT_ANCHOR_COUNT);
    *max_abs_error_m = max_abs;
}

static bool residual_vector(
    const nav_trilat_anchor_t anchors[NAV_TRILAT_ANCHOR_COUNT],
    double lat_rad,
    double lon_rad,
    double target_alt_m,
    double residuals[NAV_TRILAT_ANCHOR_COUNT],
    double expected_distances[NAV_TRILAT_ANCHOR_COUNT],
    double *rms_error_m,
    double *max_abs_error_m
)
{
    const double lat_deg = lat_rad * RAD_TO_DEG;
    const double lon_deg = normalize_lon_deg(lon_rad * RAD_TO_DEG);
    if (!is_valid_lat_lon(lat_deg, lon_deg)) {
        return false;
    }
    compute_residuals(anchors, lat_deg, lon_deg, target_alt_m, residuals, expected_distances, rms_error_m, max_abs_error_m);
    return true;
}

nav_trilat_status_t nav_trilat_solve_3_anchor_altitude(
    const nav_trilat_anchor_t anchors[NAV_TRILAT_ANCHOR_COUNT],
    double target_alt_m,
    nav_trilat_result_t *result
)
{
    if (anchors == NULL || result == NULL) {
        return NAV_TRILAT_ERR_NULL;
    }
    memset(result, 0, sizeof(*result));
    if (!isfinite(target_alt_m)) {
        return NAV_TRILAT_ERR_INVALID_INPUT;
    }
    for (int i = 0; i < NAV_TRILAT_ANCHOR_COUNT; ++i) {
        if (!is_valid_lat_lon(anchors[i].lat_deg, anchors[i].lon_deg) || !isfinite(anchors[i].alt_m) ||
            !isfinite(anchors[i].distance_m) || anchors[i].distance_m <= 0.0) {
            return NAV_TRILAT_ERR_INVALID_INPUT;
        }
    }

    result->geometry_condition = geometry_condition(anchors);
    if (!isfinite(result->geometry_condition) || result->geometry_condition > MAX_GEOMETRY_CONDITION) {
        return NAV_TRILAT_ERR_POOR_GEOMETRY;
    }

    double lat_deg = 0.0;
    double lon_deg = 0.0;
    (void)initial_guess(anchors, target_alt_m, &lat_deg, &lon_deg);

    double lat_rad = lat_deg * DEG_TO_RAD;
    double lon_rad = lon_deg * DEG_TO_RAD;
    double lambda = 1.0e-3;
    double residuals[NAV_TRILAT_ANCHOR_COUNT] = {0.0};
    double expected_distances[NAV_TRILAT_ANCHOR_COUNT] = {0.0};
    double rms = DBL_MAX;
    double max_abs = DBL_MAX;

    if (!residual_vector(anchors, lat_rad, lon_rad, target_alt_m, residuals, expected_distances, &rms, &max_abs)) {
        return NAV_TRILAT_ERR_INVALID_INPUT;
    }

    bool converged = false;
    bool accepted_candidate = false;
    for (int iter = 0; iter < MAX_ITERATIONS; ++iter) {
        result->iterations = iter + 1;

        double jac[NAV_TRILAT_ANCHOR_COUNT][2];
        for (int param = 0; param < 2; ++param) {
            const double step = FINITE_DIFF_RAD;
            double plus_residuals[NAV_TRILAT_ANCHOR_COUNT] = {0.0};
            double minus_residuals[NAV_TRILAT_ANCHOR_COUNT] = {0.0};
            double dummy_expected[NAV_TRILAT_ANCHOR_COUNT] = {0.0};
            double dummy_rms = 0.0;
            double dummy_max = 0.0;
            const double plus_lat = lat_rad + (param == 0 ? step : 0.0);
            const double plus_lon = lon_rad + (param == 1 ? step : 0.0);
            const double minus_lat = lat_rad - (param == 0 ? step : 0.0);
            const double minus_lon = lon_rad - (param == 1 ? step : 0.0);

            if (!residual_vector(anchors, plus_lat, plus_lon, target_alt_m, plus_residuals, dummy_expected, &dummy_rms, &dummy_max) ||
                !residual_vector(anchors, minus_lat, minus_lon, target_alt_m, minus_residuals, dummy_expected, &dummy_rms, &dummy_max)) {
                return NAV_TRILAT_ERR_INVALID_INPUT;
            }
            for (int i = 0; i < NAV_TRILAT_ANCHOR_COUNT; ++i) {
                jac[i][param] = (plus_residuals[i] - minus_residuals[i]) / (2.0 * step);
            }
        }

        double jtj00 = lambda;
        double jtj01 = 0.0;
        double jtj10 = 0.0;
        double jtj11 = lambda;
        double rhs0 = 0.0;
        double rhs1 = 0.0;
        for (int i = 0; i < NAV_TRILAT_ANCHOR_COUNT; ++i) {
            jtj00 += jac[i][0] * jac[i][0];
            jtj01 += jac[i][0] * jac[i][1];
            jtj10 += jac[i][1] * jac[i][0];
            jtj11 += jac[i][1] * jac[i][1];
            rhs0 -= jac[i][0] * residuals[i];
            rhs1 -= jac[i][1] * residuals[i];
        }

        double delta_lat = 0.0;
        double delta_lon = 0.0;
        if (!solve_2x2(jtj00, jtj01, jtj10, jtj11, rhs0, rhs1, &delta_lat, &delta_lon)) {
            return NAV_TRILAT_ERR_POOR_GEOMETRY;
        }

        const double max_step = 1.0e-3;
        const double step_norm = hypot(delta_lat, delta_lon);
        if (step_norm > max_step) {
            delta_lat *= max_step / step_norm;
            delta_lon *= max_step / step_norm;
        }

        double candidate_residuals[NAV_TRILAT_ANCHOR_COUNT] = {0.0};
        double candidate_expected[NAV_TRILAT_ANCHOR_COUNT] = {0.0};
        double candidate_rms = 0.0;
        double candidate_max_abs = 0.0;
        const double candidate_lat = lat_rad + delta_lat;
        const double candidate_lon = lon_rad + delta_lon;
        if (!residual_vector(anchors, candidate_lat, candidate_lon, target_alt_m, candidate_residuals, candidate_expected, &candidate_rms, &candidate_max_abs)) {
            lambda *= 10.0;
            continue;
        }

        if (candidate_rms <= rms) {
            lat_rad = candidate_lat;
            lon_rad = candidate_lon;
            memcpy(residuals, candidate_residuals, sizeof(residuals));
            memcpy(expected_distances, candidate_expected, sizeof(expected_distances));
            rms = candidate_rms;
            max_abs = candidate_max_abs;
            accepted_candidate = true;
            lambda = fmax(lambda / 10.0, 1.0e-12);

            if (hypot(delta_lat, delta_lon) < CONVERGENCE_STEP_RAD || rms < 1.0e-6) {
                converged = true;
                break;
            }
        } else {
            lambda = fmin(lambda * 10.0, 1.0e12);
        }
    }

    result->lat_deg = lat_rad * RAD_TO_DEG;
    result->lon_deg = normalize_lon_deg(lon_rad * RAD_TO_DEG);
    result->alt_m = target_alt_m;
    result->rms_error_m = rms;
    result->max_abs_error_m = max_abs;
    memcpy(result->residuals_m, residuals, sizeof(result->residuals_m));
    memcpy(result->expected_distances_m, expected_distances, sizeof(result->expected_distances_m));

    if (!converged && (!accepted_candidate || !isfinite(rms) || !isfinite(max_abs))) {
        return NAV_TRILAT_ERR_NO_CONVERGENCE;
    }
    return NAV_TRILAT_OK;
}

double nav_trilat_distance_m(
    double lat1_deg, double lon1_deg, double alt1_m,
    double lat2_deg, double lon2_deg, double alt2_m
)
{
    const nav_vec3_t a = geodetic_to_ecef(lat1_deg, lon1_deg, alt1_m);
    const nav_vec3_t b = geodetic_to_ecef(lat2_deg, lon2_deg, alt2_m);
    return vec_norm(vec_sub(a, b));
}

const char *nav_trilat_status_to_string(nav_trilat_status_t status)
{
    switch (status) {
    case NAV_TRILAT_OK:
        return "NAV_TRILAT_OK";
    case NAV_TRILAT_ERR_NULL:
        return "NAV_TRILAT_ERR_NULL";
    case NAV_TRILAT_ERR_INVALID_INPUT:
        return "NAV_TRILAT_ERR_INVALID_INPUT";
    case NAV_TRILAT_ERR_POOR_GEOMETRY:
        return "NAV_TRILAT_ERR_POOR_GEOMETRY";
    case NAV_TRILAT_ERR_NO_CONVERGENCE:
        return "NAV_TRILAT_ERR_NO_CONVERGENCE";
    default:
        return "NAV_TRILAT_ERR_UNKNOWN";
    }
}
