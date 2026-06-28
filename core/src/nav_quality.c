#include "nav/nav_quality.h"

#include <stddef.h>

static float freshness_quality(uint32_t age_ms, uint32_t ttl_ms)
{
    if (ttl_ms == 0u || age_ms >= ttl_ms) {
        return 0.0f;
    }
    return 1.0f - ((float)age_ms / (float)ttl_ms);
}

nav_anchor_quality_t nav_quality_score_anchor(
    const nav_peer_state_t *peer,
    uint32_t now_ms,
    uint32_t telemetry_ttl_ms,
    uint32_t range_ttl_ms,
    uint32_t max_range_sigma_mm
)
{
    nav_anchor_quality_t quality = {
        .gnss_quality = 0.0f,
        .telemetry_quality = 0.0f,
        .range_quality = 0.0f,
        .geometry_quality = 1.0f,
        .total_quality = 0.0f,
        .reject_reason = NAV_REJECT_NONE,
    };

    if (peer == NULL || !peer->present) {
        quality.reject_reason = NAV_REJECT_STALE_TELEMETRY;
        return quality;
    }

    const uint32_t telemetry_age = now_ms - peer->last_telemetry_timestamp_ms;
    const uint32_t range_age = now_ms - peer->last_range_timestamp_ms;
    if (telemetry_ttl_ms > 0u && telemetry_age > telemetry_ttl_ms) {
        quality.reject_reason = NAV_REJECT_STALE_TELEMETRY;
        return quality;
    }
    if (range_ttl_ms > 0u && range_age > range_ttl_ms) {
        quality.reject_reason = NAV_REJECT_STALE_RANGE;
        return quality;
    }
    if (!peer->gnss_valid || peer->fix_type < NAV_GNSS_FIX_3D) {
        quality.reject_reason = NAV_REJECT_BAD_GNSS;
        return quality;
    }
    if (!peer->range_valid || peer->range_mm == 0u) {
        quality.reject_reason = NAV_REJECT_STALE_RANGE;
        return quality;
    }
    if (max_range_sigma_mm > 0u && peer->range_sigma_mm > max_range_sigma_mm) {
        quality.reject_reason = NAV_REJECT_BAD_RANGE_SIGMA;
        return quality;
    }

    quality.gnss_quality = 1.0f;
    quality.telemetry_quality = freshness_quality(telemetry_age, telemetry_ttl_ms);
    quality.range_quality = freshness_quality(range_age, range_ttl_ms);
    if (telemetry_ttl_ms == 0u) {
        quality.telemetry_quality = 1.0f;
    }
    if (range_ttl_ms == 0u) {
        quality.range_quality = 1.0f;
    }
    quality.total_quality = quality.gnss_quality * quality.telemetry_quality * quality.range_quality * quality.geometry_quality;
    return quality;
}

const char *nav_reject_reason_to_string(nav_reject_reason_t reason)
{
    switch (reason) {
    case NAV_REJECT_NONE:
        return "NONE";
    case NAV_REJECT_STALE_TELEMETRY:
        return "STALE_TELEMETRY";
    case NAV_REJECT_STALE_RANGE:
        return "STALE_RANGE";
    case NAV_REJECT_BAD_GNSS:
        return "BAD_GNSS";
    case NAV_REJECT_BAD_POSITION:
        return "BAD_POSITION";
    case NAV_REJECT_BAD_RANGE_SIGMA:
        return "BAD_RANGE_SIGMA";
    case NAV_REJECT_RANGE_OUTLIER:
        return "RANGE_OUTLIER";
    case NAV_REJECT_BAD_GEOMETRY:
        return "BAD_GEOMETRY";
    case NAV_REJECT_NOT_ENOUGH_ANCHORS:
        return "NOT_ENOUGH_ANCHORS";
    case NAV_REJECT_MISSING_LOCAL_ALTITUDE:
        return "MISSING_LOCAL_ALTITUDE";
    case NAV_REJECT_TRILATERATION_FAILED:
        return "TRILATERATION_FAILED";
    default:
        return "UNKNOWN";
    }
}

const char *nav_altitude_source_to_string(nav_altitude_source_t source)
{
    switch (source) {
    case NAV_ALT_SOURCE_NONE:
        return "NONE";
    case NAV_ALT_SOURCE_GNSS:
        return "GNSS";
    case NAV_ALT_SOURCE_BARO:
        return "BARO";
    case NAV_ALT_SOURCE_FC:
        return "FC";
    case NAV_ALT_SOURCE_SIM:
        return "SIM";
    case NAV_ALT_SOURCE_MANUAL:
        return "MANUAL";
    default:
        return "UNKNOWN";
    }
}

const char *nav_solution_source_to_string(nav_solution_source_t source)
{
    switch (source) {
    case NAV_SOURCE_NONE:
        return "NONE";
    case NAV_SOURCE_LOCAL_GNSS:
        return "LOCAL_GNSS";
    case NAV_SOURCE_RADIO_3D:
        return "RADIO_3D";
    case NAV_SOURCE_REPLAY:
        return "REPLAY";
    case NAV_SOURCE_SIM:
        return "SIM";
    default:
        return "UNKNOWN";
    }
}

const char *nav_solution_status_to_string(nav_solution_status_t status)
{
    switch (status) {
    case NAV_SOLUTION_NONE:
        return "NONE";
    case NAV_SOLUTION_GNSS_DIRECT:
        return "GNSS_DIRECT";
    case NAV_SOLUTION_RADIO_3D:
        return "RADIO_3D";
    case NAV_SOLUTION_DEGRADED:
        return "DEGRADED";
    case NAV_SOLUTION_REJECTED:
        return "REJECTED";
    default:
        return "UNKNOWN";
    }
}

const char *nav_range_fail_reason_to_string(nav_range_fail_reason_t reason)
{
    switch (reason) {
    case NAV_RANGE_FAIL_NONE:
        return "NONE";
    case NAV_RANGE_FAIL_TIMEOUT:
        return "TIMEOUT";
    case NAV_RANGE_FAIL_NO_RESPONSE:
        return "NO_RESPONSE";
    case NAV_RANGE_FAIL_RADIO_BUSY:
        return "RADIO_BUSY";
    case NAV_RANGE_FAIL_BAD_FRAME:
        return "BAD_FRAME";
    case NAV_RANGE_FAIL_RANGING_ENGINE_ERROR:
        return "RANGING_ENGINE_ERROR";
    case NAV_RANGE_FAIL_ABORTED:
        return "ABORTED";
    case NAV_RANGE_FAIL_UNKNOWN:
        return "UNKNOWN";
    default:
        return "UNKNOWN";
    }
}
