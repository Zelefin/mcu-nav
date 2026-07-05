#include "nav/nav_core.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "nav/nav_gnss.h"
#include "nav/nav_quality.h"
#include "nav/nav_state_machine.h"
#include "nav/nav_trilateration.h"

#define NAV_DEG_TO_RAD 0.01745329251994329576923690768489
#define NAV_EARTH_RADIUS_M 6378137.0

nav_config_t nav_config_default(uint8_t local_node_id)
{
    nav_config_t config = {
        .local_node_id = local_node_id,
        .telemetry_ttl_ms = 2000u,
        .range_ttl_ms = 1000u,
        .local_altitude_ttl_ms = 1000u,
        .tick_period_ms = 100u,
        .radio_solve_interval_ms = 500u,
        .max_range_sigma_mm = 10000u,
        .min_anchor_quality = 0.05f,
        .min_solution_quality = 0.20f,
        .max_residual_rms_m = 2.0f,
        .max_residual_m = 5.0f,
        .min_anchor_triangle_area_m2 = 100.0f,
        .degraded_anchor_triangle_area_m2 = 1000.0f,
        .demo_force_gps_denied = false,
        .allow_gnss_altitude_in_demo_forced_denied = false,
        .ignore_altitude_for_radio_solve = false,
        .retain_last_range_on_failure = false,
    };
    return config;
}

static void emit_log(
    const nav_system_t *sys,
    uint32_t now_ms,
    nav_log_level_t level,
    nav_log_category_t category,
    const char *event,
    const char *message
)
{
    nav_log_emit(&sys->logger, now_ms, level, category, event, message);
}

static bool position_valid(nav_position_t position)
{
    return position.lat_e7 >= -900000000 && position.lat_e7 <= 900000000 &&
           position.lon_e7 >= -1800000000 && position.lon_e7 <= 1800000000;
}

static bool peer_position_valid(nav_position_t position)
{
    if (!position_valid(position)) {
        return false;
    }
    return !(position.lat_e7 == 0 && position.lon_e7 == 0 && position.alt_mm == 0);
}

static void snapshot_clear_diagnostics(nav_snapshot_t *snapshot)
{
    snapshot->num_anchors = 0u;
    snapshot->rejected_count = 0u;
    snapshot->residual_rms_m = 0.0f;
    snapshot->max_residual_m = 0.0f;
    snapshot->geometry_score = 0.0f;
    snapshot->anchor_triangle_area_m2 = 0.0f;
    snapshot->total_quality = 0.0f;
    snapshot->altitude_source = NAV_ALT_SOURCE_NONE;
    snapshot->local_altitude_valid = false;
    snapshot->solution_source = NAV_SOURCE_NONE;
    for (size_t i = 0u; i < NAV_TRILAT_ANCHOR_COUNT; ++i) {
        snapshot->selected_anchor_node_ids[i] = NAV_INVALID_NODE_ID;
        snapshot->anchor_residuals_mm[i] = 0;
    }
    for (size_t i = 0u; i < NAV_MAX_NODES; ++i) {
        snapshot->rejected_node_ids[i] = NAV_INVALID_NODE_ID;
        snapshot->rejected_reasons[i] = NAV_REJECT_NONE;
    }
}

static void populate_local_gnss_evidence(nav_system_t *sys, uint32_t now_ms)
{
    sys->snapshot.local_gnss_present = sys->local_gnss_present;
    sys->snapshot.local_gnss_valid = false;
    sys->snapshot.local_gnss_used = false;
    sys->snapshot.local_gnss_age_ms = 0u;
    sys->snapshot.local_gnss_fix_type = NAV_GNSS_FIX_NONE;
    sys->snapshot.local_gnss_satellites = 0u;
    sys->snapshot.local_gnss_hdop_centi = 0u;
    sys->snapshot.local_gnss_hacc_mm = 0u;
    sys->snapshot.local_gnss_vacc_mm = 0u;
    sys->snapshot.local_gnss_position = (nav_position_t){0};

    if (!sys->local_gnss_present) {
        return;
    }

    sys->snapshot.local_gnss_valid = nav_gnss_sample_is_usable(&sys->local_gnss);
    sys->snapshot.local_gnss_age_ms = now_ms - sys->local_gnss.timestamp_ms;
    sys->snapshot.local_gnss_fix_type = sys->local_gnss.fix_type;
    sys->snapshot.local_gnss_satellites = sys->local_gnss.satellites;
    sys->snapshot.local_gnss_hdop_centi = sys->local_gnss.hdop_centi;
    sys->snapshot.local_gnss_hacc_mm = sys->local_gnss.hacc_mm;
    sys->snapshot.local_gnss_vacc_mm = sys->local_gnss.vacc_mm;
    sys->snapshot.local_gnss_position = sys->local_gnss.position;
}

static void mark_radio_solve_inputs_changed(nav_system_t *sys)
{
    ++sys->radio_solve_generation;
    if (sys->radio_solve_generation == 0u) {
        ++sys->radio_solve_generation;
    }
}

static bool local_gnss_can_affect_radio_solution(const nav_system_t *sys)
{
    return sys->config.demo_force_gps_denied && sys->config.allow_gnss_altitude_in_demo_forced_denied &&
           !sys->config.ignore_altitude_for_radio_solve;
}

static void selection_init(nav_anchor_selection_t *selection)
{
    memset(selection, 0, sizeof(*selection));
    selection->reject_reason = NAV_REJECT_NONE;
    for (size_t i = 0u; i < NAV_MAX_NODES; ++i) {
        selection->rejected_node_ids[i] = NAV_INVALID_NODE_ID;
        selection->rejected_reasons[i] = NAV_REJECT_NONE;
    }
}

static void selection_add_rejection(nav_anchor_selection_t *selection, uint8_t node_id, nav_reject_reason_t reason)
{
    if (selection->rejected_count >= NAV_MAX_NODES) {
        return;
    }
    selection->rejected_node_ids[selection->rejected_count] = node_id;
    selection->rejected_reasons[selection->rejected_count] = reason;
    ++selection->rejected_count;
}

static void selection_add_candidate(nav_anchor_selection_t *selection, nav_anchor_t anchor)
{
    if (selection->count < NAV_TRILAT_ANCHOR_COUNT) {
        selection->anchors[selection->count] = anchor;
        ++selection->count;
        return;
    }

    size_t worst = 0u;
    for (size_t i = 1u; i < NAV_TRILAT_ANCHOR_COUNT; ++i) {
        if (selection->anchors[i].quality < selection->anchors[worst].quality) {
            worst = i;
        }
    }
    if (anchor.quality > selection->anchors[worst].quality) {
        selection_add_rejection(selection, selection->anchors[worst].node_id, NAV_REJECT_NONE);
        selection->anchors[worst] = anchor;
    } else {
        selection_add_rejection(selection, anchor.node_id, NAV_REJECT_NONE);
    }
}

static bool local_altitude_is_fresh(nav_local_altitude_t altitude, uint32_t now_ms, uint32_t ttl_ms)
{
    if (!altitude.valid || altitude.source == NAV_ALT_SOURCE_NONE) {
        return false;
    }
    return ttl_ms == 0u || (now_ms - altitude.timestamp_ms) <= ttl_ms;
}

static bool resolve_local_altitude(const nav_system_t *sys, uint32_t now_ms, nav_local_altitude_t *out)
{
    if (sys->local_altitude_present && local_altitude_is_fresh(sys->local_altitude, now_ms, sys->config.local_altitude_ttl_ms)) {
        *out = sys->local_altitude;
        return true;
    }

    const bool gnss_alt_allowed = !sys->config.demo_force_gps_denied || sys->config.allow_gnss_altitude_in_demo_forced_denied;
    if (gnss_alt_allowed && sys->local_gnss_present && nav_gnss_sample_is_usable(&sys->local_gnss)) {
        const uint32_t age_ms = now_ms - sys->local_gnss.timestamp_ms;
        if (sys->config.local_altitude_ttl_ms == 0u || age_ms <= sys->config.local_altitude_ttl_ms) {
            out->alt_mm = sys->local_gnss.position.alt_mm;
            out->timestamp_ms = sys->local_gnss.timestamp_ms;
            out->source = NAV_ALT_SOURCE_GNSS;
            out->valid = true;
            return true;
        }
    }
    return false;
}

static double anchor_x_m(const nav_anchor_t *anchor, double ref_lat_rad, double ref_lon_rad)
{
    const double lon_rad = ((double)anchor->lon_e7 / 10000000.0) * NAV_DEG_TO_RAD;
    return (lon_rad - ref_lon_rad) * cos(ref_lat_rad) * NAV_EARTH_RADIUS_M;
}

static double anchor_y_m(const nav_anchor_t *anchor, double ref_lat_rad)
{
    const double lat_rad = ((double)anchor->lat_e7 / 10000000.0) * NAV_DEG_TO_RAD;
    return (lat_rad - ref_lat_rad) * NAV_EARTH_RADIUS_M;
}

static float compute_anchor_triangle_area_m2(const nav_anchor_t anchors[NAV_TRILAT_ANCHOR_COUNT])
{
    const double ref_lat_rad = ((double)anchors[0].lat_e7 / 10000000.0) * NAV_DEG_TO_RAD;
    const double ref_lon_rad = ((double)anchors[0].lon_e7 / 10000000.0) * NAV_DEG_TO_RAD;
    const double x0 = 0.0;
    const double y0 = 0.0;
    const double x1 = anchor_x_m(&anchors[1], ref_lat_rad, ref_lon_rad);
    const double y1 = anchor_y_m(&anchors[1], ref_lat_rad);
    const double x2 = anchor_x_m(&anchors[2], ref_lat_rad, ref_lon_rad);
    const double y2 = anchor_y_m(&anchors[2], ref_lat_rad);
    const double area = 0.5 * fabs((x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0));
    return (float)area;
}

static float geometry_score_from_area(float area_m2, const nav_config_t *config)
{
    if (config->degraded_anchor_triangle_area_m2 <= 0.0f || area_m2 >= config->degraded_anchor_triangle_area_m2) {
        return 1.0f;
    }
    if (area_m2 <= 0.0f) {
        return 0.0f;
    }
    return area_m2 / config->degraded_anchor_triangle_area_m2;
}

static void copy_selection_to_snapshot(nav_snapshot_t *snapshot, const nav_anchor_selection_t *selection)
{
    snapshot->num_anchors = (uint8_t)selection->count;
    snapshot->rejected_count = selection->rejected_count;
    for (size_t i = 0u; i < selection->count && i < NAV_TRILAT_ANCHOR_COUNT; ++i) {
        snapshot->selected_anchor_node_ids[i] = selection->anchors[i].node_id;
    }
    for (size_t i = 0u; i < selection->rejected_count && i < NAV_MAX_NODES; ++i) {
        snapshot->rejected_node_ids[i] = selection->rejected_node_ids[i];
        snapshot->rejected_reasons[i] = selection->rejected_reasons[i];
    }
}

static void emit_mode_status_changes(
    nav_system_t *sys,
    uint32_t now_ms,
    nav_mode_t old_mode,
    nav_solution_status_t old_status
)
{
    char message[128];
    if (old_mode != sys->snapshot.nav_mode) {
        (void)snprintf(
            message,
            sizeof(message),
            "from=%s to=%s",
            nav_mode_to_string(old_mode),
            nav_mode_to_string(sys->snapshot.nav_mode)
        );
        emit_log(sys, now_ms, NAV_LOG_INFO, NAV_LOG_CAT_STATE, "mode_transition", message);
    }
    if (old_status != sys->snapshot.solution_status) {
        (void)snprintf(
            message,
            sizeof(message),
            "from=%s to=%s",
            nav_solution_status_to_string(old_status),
            nav_solution_status_to_string(sys->snapshot.solution_status)
        );
        emit_log(sys, now_ms, NAV_LOG_INFO, NAV_LOG_CAT_SOLUTION, "solution_status_transition", message);
    }
}

nav_status_t nav_select_radio_anchors(const nav_system_t *sys, uint32_t now_ms, nav_anchor_selection_t *out)
{
    if (sys == NULL || out == NULL) {
        return NAV_STATUS_INVALID_ARGUMENT;
    }
    selection_init(out);

    for (uint8_t node_id = 0u; node_id < NAV_MAX_NODES; ++node_id) {
        if (node_id == sys->config.local_node_id) {
            continue;
        }

        const nav_peer_state_t *peer = nav_peer_table_get_const(&sys->peer_table, node_id);
        nav_anchor_quality_t quality = nav_quality_score_anchor(
            peer,
            now_ms,
            sys->config.telemetry_ttl_ms,
            sys->config.range_ttl_ms,
            sys->config.max_range_sigma_mm
        );
        nav_reject_reason_t reason = quality.reject_reason;
        if (reason == NAV_REJECT_NONE && peer != NULL && (!peer->range_position_valid || !peer_position_valid(peer->range_position))) {
            reason = NAV_REJECT_BAD_POSITION;
        }
        if (reason == NAV_REJECT_NONE && quality.total_quality < sys->config.min_anchor_quality) {
            reason = NAV_REJECT_STALE_TELEMETRY;
        }
        if (reason != NAV_REJECT_NONE || peer == NULL) {
            selection_add_rejection(out, node_id, reason);
            continue;
        }
        nav_anchor_t anchor = {
            .node_id = node_id,
            .lat_e7 = peer->range_position.lat_e7,
            .lon_e7 = peer->range_position.lon_e7,
            .alt_mm = peer->range_position.alt_mm,
            .range_mm = peer->range_mm,
            .range_sigma_mm = peer->range_sigma_mm,
            .quality = quality.total_quality,
            .reject_reason = NAV_REJECT_NONE,
        };
        selection_add_candidate(out, anchor);
    }

    if (out->count < NAV_TRILAT_ANCHOR_COUNT) {
        out->reject_reason = NAV_REJECT_NOT_ENOUGH_ANCHORS;
        return NAV_STATUS_NOT_FOUND;
    }
    return NAV_STATUS_OK;
}

static void update_peer_diagnostics_from_selection(nav_system_t *sys, const nav_anchor_selection_t *selection)
{
    for (uint8_t node_id = 0u; node_id < NAV_MAX_NODES; ++node_id) {
        nav_peer_state_t *peer = nav_peer_table_get(&sys->peer_table, node_id);
        if (peer != NULL) {
            peer->anchor_quality = 0.0f;
            peer->last_reject_reason = NAV_REJECT_NONE;
        }
    }
    for (size_t i = 0u; i < selection->count; ++i) {
        nav_peer_state_t *peer = nav_peer_table_get(&sys->peer_table, selection->anchors[i].node_id);
        if (peer != NULL) {
            peer->anchor_quality = selection->anchors[i].quality;
            peer->last_reject_reason = NAV_REJECT_NONE;
        }
    }
    for (size_t i = 0u; i < selection->rejected_count; ++i) {
        nav_peer_state_t *peer = nav_peer_table_get(&sys->peer_table, selection->rejected_node_ids[i]);
        if (peer != NULL) {
            peer->anchor_quality = 0.0f;
            peer->last_reject_reason = selection->rejected_reasons[i];
        }
    }
}

static void log_anchor_selection(nav_system_t *sys, uint32_t now_ms, const nav_anchor_selection_t *selection)
{
    char message[160];
    for (size_t i = 0u; i < selection->count; ++i) {
        (void)snprintf(
            message,
            sizeof(message),
            "peer=%u quality=%.3f range_mm=%lu range_sigma_mm=%lu",
            selection->anchors[i].node_id,
            (double)selection->anchors[i].quality,
            (unsigned long)selection->anchors[i].range_mm,
            (unsigned long)selection->anchors[i].range_sigma_mm
        );
        emit_log(sys, now_ms, NAV_LOG_DEBUG, NAV_LOG_CAT_QUALITY, "anchor_accepted", message);
    }
    for (size_t i = 0u; i < selection->rejected_count; ++i) {
        (void)snprintf(
            message,
            sizeof(message),
            "peer=%u reason=%s",
            selection->rejected_node_ids[i],
            nav_reject_reason_to_string(selection->rejected_reasons[i])
        );
        emit_log(sys, now_ms, NAV_LOG_WARN, NAV_LOG_CAT_QUALITY, "anchor_rejected", message);
    }
}

static void set_rejected_snapshot(nav_system_t *sys, uint32_t now_ms, nav_reject_reason_t reason, nav_mode_t mode)
{
    sys->snapshot.time_ms = now_ms;
    sys->snapshot.node_id = sys->config.local_node_id;
    sys->snapshot.nav_mode = mode;
    sys->snapshot.solution_status = NAV_SOLUTION_REJECTED;
    sys->snapshot.solution_source = NAV_SOURCE_NONE;
    sys->snapshot.reject_reason = reason;
    sys->snapshot.position = (nav_position_t){0};
    sys->snapshot.hacc_mm = 0u;
    sys->snapshot.vacc_mm = 0u;
}

static void reuse_previous_radio_snapshot(nav_system_t *sys, const nav_snapshot_t *previous, uint32_t now_ms)
{
    sys->snapshot = *previous;
    sys->snapshot.time_ms = now_ms;
    sys->snapshot.node_id = sys->config.local_node_id;
    populate_local_gnss_evidence(sys, now_ms);
}

static void attempt_radio_solution(nav_system_t *sys, uint32_t now_ms, const nav_snapshot_t *previous)
{
    char message[192];
    if (sys->radio_solve_ran) {
        const uint32_t elapsed_ms = now_ms - sys->last_radio_solve_ms;
        if (sys->config.radio_solve_interval_ms > 0u && elapsed_ms < sys->config.radio_solve_interval_ms) {
            reuse_previous_radio_snapshot(sys, previous, now_ms);
            (void)snprintf(
                message,
                sizeof(message),
                "reason=CADENCE elapsed_ms=%lu interval_ms=%lu",
                (unsigned long)elapsed_ms,
                (unsigned long)sys->config.radio_solve_interval_ms
            );
            emit_log(sys, now_ms, NAV_LOG_DEBUG, NAV_LOG_CAT_SOLUTION, "solve_skipped", message);
            return;
        }
    }

    nav_anchor_selection_t selection;
    const nav_status_t select_status = nav_select_radio_anchors(sys, now_ms, &selection);
    update_peer_diagnostics_from_selection(sys, &selection);
    copy_selection_to_snapshot(&sys->snapshot, &selection);
    log_anchor_selection(sys, now_ms, &selection);
    sys->last_radio_solve_ms = now_ms;
    sys->radio_solve_ran = true;

    if (select_status != NAV_STATUS_OK) {
        set_rejected_snapshot(sys, now_ms, selection.reject_reason, NAV_MODE_NO_NAV_SOLUTION);
        (void)snprintf(message, sizeof(message), "reason=%s anchors=%zu", nav_reject_reason_to_string(selection.reject_reason), selection.count);
        emit_log(sys, now_ms, NAV_LOG_WARN, NAV_LOG_CAT_SOLUTION, "solve_rejected", message);
        return;
    }

    nav_local_altitude_t altitude = {0};
    if (!resolve_local_altitude(sys, now_ms, &altitude)) {
        set_rejected_snapshot(sys, now_ms, NAV_REJECT_MISSING_LOCAL_ALTITUDE, NAV_MODE_NO_NAV_SOLUTION);
        copy_selection_to_snapshot(&sys->snapshot, &selection);
        emit_log(sys, now_ms, NAV_LOG_WARN, NAV_LOG_CAT_SOLUTION, "solve_rejected", "reason=MISSING_LOCAL_ALTITUDE");
        return;
    }
    sys->snapshot.local_altitude_valid = true;
    sys->snapshot.altitude_source = altitude.source;

    const float area_m2 = compute_anchor_triangle_area_m2(selection.anchors);
    const float geometry_score = geometry_score_from_area(area_m2, &sys->config);
    sys->snapshot.anchor_triangle_area_m2 = area_m2;
    sys->snapshot.geometry_score = geometry_score;
    if (area_m2 < sys->config.min_anchor_triangle_area_m2) {
        set_rejected_snapshot(sys, now_ms, NAV_REJECT_BAD_GEOMETRY, NAV_MODE_NO_NAV_SOLUTION);
        copy_selection_to_snapshot(&sys->snapshot, &selection);
        sys->snapshot.anchor_triangle_area_m2 = area_m2;
        sys->snapshot.geometry_score = geometry_score;
        (void)snprintf(message, sizeof(message), "reason=BAD_GEOMETRY triangle_area_m2=%.3f", (double)area_m2);
        emit_log(sys, now_ms, NAV_LOG_WARN, NAV_LOG_CAT_SOLUTION, "solve_rejected", message);
        return;
    }

    if (sys->radio_solve_generation == sys->last_radio_solve_generation) {
        reuse_previous_radio_snapshot(sys, previous, now_ms);
        (void)snprintf(
            message,
            sizeof(message),
            "reason=UNCHANGED_INPUTS generation=%lu",
            (unsigned long)sys->radio_solve_generation
        );
        emit_log(sys, now_ms, NAV_LOG_DEBUG, NAV_LOG_CAT_SOLUTION, "solve_skipped", message);
        return;
    }

    nav_trilat_anchor_t trilat_anchors[NAV_TRILAT_ANCHOR_COUNT];
    float average_anchor_quality = 0.0f;
    for (size_t i = 0u; i < NAV_TRILAT_ANCHOR_COUNT; ++i) {
        trilat_anchors[i].lat_deg = (double)selection.anchors[i].lat_e7 / 10000000.0;
        trilat_anchors[i].lon_deg = (double)selection.anchors[i].lon_e7 / 10000000.0;
        trilat_anchors[i].alt_m =
            sys->config.ignore_altitude_for_radio_solve ? 0.0 : (double)selection.anchors[i].alt_mm / 1000.0;
        trilat_anchors[i].distance_m = (double)selection.anchors[i].range_mm / 1000.0;
        trilat_anchors[i].node_id = selection.anchors[i].node_id;
        average_anchor_quality += selection.anchors[i].quality / (float)NAV_TRILAT_ANCHOR_COUNT;
    }

    (void)snprintf(
        message,
        sizeof(message),
        "anchors=%u,%u,%u target_alt_mm=%ld generation=%lu interval_ms=%lu",
        selection.anchors[0].node_id,
        selection.anchors[1].node_id,
        selection.anchors[2].node_id,
        (long)altitude.alt_mm,
        (unsigned long)sys->radio_solve_generation,
        (unsigned long)sys->config.radio_solve_interval_ms
    );
    emit_log(sys, now_ms, NAV_LOG_INFO, NAV_LOG_CAT_SOLUTION, "solve_attempt", message);

    nav_trilat_result_t result;
    const nav_trilat_status_t trilat_status =
        nav_trilat_solve_3_anchor_altitude(
            trilat_anchors,
            sys->config.ignore_altitude_for_radio_solve ? 0.0 : (double)altitude.alt_mm / 1000.0,
            &result
        );
    sys->last_radio_solve_generation = sys->radio_solve_generation;
    if (trilat_status != NAV_TRILAT_OK) {
        set_rejected_snapshot(sys, now_ms, NAV_REJECT_TRILATERATION_FAILED, NAV_MODE_NO_NAV_SOLUTION);
        copy_selection_to_snapshot(&sys->snapshot, &selection);
        sys->snapshot.local_altitude_valid = true;
        sys->snapshot.altitude_source = altitude.source;
        (void)snprintf(message, sizeof(message), "reason=TRILATERATION_FAILED status=%s", nav_trilat_status_to_string(trilat_status));
        emit_log(sys, now_ms, NAV_LOG_ERROR, NAV_LOG_CAT_SOLUTION, "solve_rejected", message);
        return;
    }

    sys->snapshot.position.lat_e7 = (int32_t)llround(result.lat_deg * 10000000.0);
    sys->snapshot.position.lon_e7 = (int32_t)llround(result.lon_deg * 10000000.0);
    sys->snapshot.position.alt_mm = altitude.alt_mm;
    sys->snapshot.residual_rms_m = (float)result.rms_error_m;
    sys->snapshot.max_residual_m = (float)result.max_abs_error_m;
    for (size_t i = 0u; i < NAV_TRILAT_ANCHOR_COUNT; ++i) {
        sys->snapshot.anchor_residuals_mm[i] = (int32_t)llround(result.residuals_m[i] * 1000.0);
        nav_peer_state_t *peer = nav_peer_table_get(&sys->peer_table, selection.anchors[i].node_id);
        if (peer != NULL) {
            peer->last_residual_m = (float)result.residuals_m[i];
        }
    }
    sys->snapshot.hacc_mm = (uint32_t)llround(fabs(result.max_abs_error_m) * 1000.0);
    sys->snapshot.vacc_mm = 0u;
    sys->snapshot.total_quality = average_anchor_quality * geometry_score;
    sys->snapshot.reject_reason = NAV_REJECT_NONE;

    if (sys->snapshot.residual_rms_m > sys->config.max_residual_rms_m || sys->snapshot.max_residual_m > sys->config.max_residual_m) {
        sys->snapshot.nav_mode = NAV_MODE_NO_NAV_SOLUTION;
        sys->snapshot.solution_status = NAV_SOLUTION_REJECTED;
        sys->snapshot.solution_source = NAV_SOURCE_NONE;
        sys->snapshot.reject_reason = NAV_REJECT_RANGE_OUTLIER;
        (void)snprintf(
            message,
            sizeof(message),
            "reason=RANGE_OUTLIER residual_rms_m=%.6f max_residual_m=%.6f",
            (double)sys->snapshot.residual_rms_m,
            (double)sys->snapshot.max_residual_m
        );
        emit_log(sys, now_ms, NAV_LOG_WARN, NAV_LOG_CAT_SOLUTION, "solve_rejected", message);
        return;
    }

    if (sys->snapshot.total_quality < sys->config.min_solution_quality || geometry_score < 1.0f) {
        sys->snapshot.nav_mode = NAV_MODE_RADIO_NAV_DEGRADED;
        sys->snapshot.solution_status = NAV_SOLUTION_DEGRADED;
        sys->snapshot.solution_source = NAV_SOURCE_RADIO_3D;
    } else {
        sys->snapshot.nav_mode = NAV_MODE_RADIO_NAV_OK;
        sys->snapshot.solution_status = NAV_SOLUTION_RADIO_3D;
        sys->snapshot.solution_source = NAV_SOURCE_RADIO_3D;
    }

    (void)snprintf(
        message,
        sizeof(message),
        "lat_e7=%ld lon_e7=%ld alt_mm=%ld residual_rms_m=%.6f max_residual_m=%.6f quality=%.3f geometry_score=%.3f iterations=%d",
        (long)sys->snapshot.position.lat_e7,
        (long)sys->snapshot.position.lon_e7,
        (long)sys->snapshot.position.alt_mm,
        (double)sys->snapshot.residual_rms_m,
        (double)sys->snapshot.max_residual_m,
        (double)sys->snapshot.total_quality,
        (double)sys->snapshot.geometry_score,
        result.iterations
    );
    emit_log(sys, now_ms, NAV_LOG_INFO, NAV_LOG_CAT_SOLUTION, "solve_succeeded", message);

    for (size_t i = 0u; i < NAV_TRILAT_ANCHOR_COUNT; ++i) {
        (void)snprintf(
            message,
            sizeof(message),
            "peer=%u residual_mm=%ld expected_range_m=%.6f",
            selection.anchors[i].node_id,
            (long)sys->snapshot.anchor_residuals_mm[i],
            result.expected_distances_m[i]
        );
        emit_log(sys, now_ms, NAV_LOG_DEBUG, NAV_LOG_CAT_SOLUTION, "residual", message);
    }
}

static void update_snapshot(nav_system_t *sys, uint32_t now_ms, bool allow_radio_solve)
{
    const nav_mode_t old_mode = sys->snapshot.nav_mode;
    const nav_solution_status_t old_status = sys->snapshot.solution_status;
    const bool local_usable = nav_gnss_sample_is_usable(&sys->local_gnss) && !sys->config.demo_force_gps_denied;
    const nav_snapshot_t previous = sys->snapshot;

    snapshot_clear_diagnostics(&sys->snapshot);
    sys->snapshot.time_ms = now_ms;
    sys->snapshot.node_id = sys->config.local_node_id;
    populate_local_gnss_evidence(sys, now_ms);

    if (local_usable) {
        sys->snapshot.nav_mode = NAV_MODE_GNSS_OK;
        sys->snapshot.solution_status = NAV_SOLUTION_GNSS_DIRECT;
        sys->snapshot.solution_source = NAV_SOURCE_LOCAL_GNSS;
        sys->snapshot.position = sys->local_gnss.position;
        sys->snapshot.hacc_mm = sys->local_gnss.hacc_mm;
        sys->snapshot.vacc_mm = sys->local_gnss.vacc_mm;
        sys->snapshot.total_quality = 1.0f;
        sys->snapshot.reject_reason = NAV_REJECT_NONE;
        sys->snapshot.local_altitude_valid = true;
        sys->snapshot.altitude_source = NAV_ALT_SOURCE_GNSS;
        sys->snapshot.local_gnss_used = true;
    } else if (allow_radio_solve) {
        attempt_radio_solution(sys, now_ms, &previous);
    } else {
        reuse_previous_radio_snapshot(sys, &previous, now_ms);
    }

    sys->mode = sys->snapshot.nav_mode;
    emit_mode_status_changes(sys, now_ms, old_mode, old_status);
}

void nav_core_init(nav_system_t *sys, const nav_config_t *config)
{
    if (sys == NULL) {
        return;
    }
    memset(sys, 0, sizeof(*sys));
    sys->config = config == NULL ? nav_config_default(0u) : *config;
    nav_peer_table_init(&sys->peer_table);
    sys->mode = NAV_MODE_BOOT;
    sys->snapshot.node_id = sys->config.local_node_id;
    sys->snapshot.nav_mode = NAV_MODE_BOOT;
    sys->snapshot.solution_status = NAV_SOLUTION_NONE;
    sys->snapshot.solution_source = NAV_SOURCE_NONE;
    sys->snapshot.reject_reason = NAV_REJECT_NONE;
    snapshot_clear_diagnostics(&sys->snapshot);
}

void nav_core_set_logger(nav_system_t *sys, const nav_logger_t *logger)
{
    if (sys == NULL || logger == NULL) {
        return;
    }
    sys->logger = *logger;
    emit_log(sys, 0u, NAV_LOG_INFO, NAV_LOG_CAT_BOOT, "core_init", "nav core initialized");
}

void nav_core_handle_event(nav_system_t *sys, const nav_event_t *event)
{
    if (sys == NULL || event == NULL) {
        return;
    }

    char message[160];
    const uint32_t now_ms = event->timestamp_ms;
    switch (event->type) {
    case NAV_EVT_TICK:
        nav_core_tick(sys, now_ms);
        break;
    case NAV_EVT_LOCAL_GNSS_SAMPLE:
        sys->local_gnss = event->data.local_gnss;
        sys->local_gnss_present = true;
        if (local_gnss_can_affect_radio_solution(sys)) {
            mark_radio_solve_inputs_changed(sys);
        }
        (void)snprintf(
            message,
            sizeof(message),
            "valid=%u fix_type=%d lat_e7=%ld lon_e7=%ld alt_mm=%ld forced_denied=%u",
            sys->local_gnss.valid ? 1u : 0u,
            (int)sys->local_gnss.fix_type,
            (long)sys->local_gnss.position.lat_e7,
            (long)sys->local_gnss.position.lon_e7,
            (long)sys->local_gnss.position.alt_mm,
            sys->config.demo_force_gps_denied ? 1u : 0u
        );
        emit_log(sys, now_ms, NAV_LOG_INFO, NAV_LOG_CAT_GNSS, "local_gnss_sample", message);
        update_snapshot(sys, now_ms, false);
        break;
    case NAV_EVT_LOCAL_ALTITUDE_SAMPLE: {
        const bool altitude_changed =
            !sys->local_altitude_present ||
            sys->local_altitude.alt_mm != event->data.local_altitude.alt_mm ||
            sys->local_altitude.source != event->data.local_altitude.source ||
            sys->local_altitude.valid != event->data.local_altitude.valid;
        sys->local_altitude = event->data.local_altitude;
        sys->local_altitude_present = true;
        if (altitude_changed && !sys->config.ignore_altitude_for_radio_solve) {
            mark_radio_solve_inputs_changed(sys);
        }
        (void)snprintf(
            message,
            sizeof(message),
            "valid=%u source=%s alt_mm=%ld",
            sys->local_altitude.valid ? 1u : 0u,
            nav_altitude_source_to_string(sys->local_altitude.source),
            (long)sys->local_altitude.alt_mm
        );
        emit_log(
            sys,
            now_ms,
            sys->local_altitude.valid ? NAV_LOG_INFO : NAV_LOG_WARN,
            NAV_LOG_CAT_GNSS,
            sys->local_altitude.valid ? "local_altitude_accepted" : "local_altitude_rejected",
            message
        );
        update_snapshot(sys, now_ms, false);
        break;
    }
    case NAV_EVT_PEER_TELEMETRY_RX:
        if (nav_peer_table_update_beacon_rx(&sys->peer_table, &event->data.peer_beacon_rx, now_ms)) {
            mark_radio_solve_inputs_changed(sys);
            (void)snprintf(
                message,
                sizeof(message),
                "peer=%u packet_seq=%lu lat_e7=%ld lon_e7=%ld alt_mm=%ld gnss_valid=%u rssi_dbm=%d snr_db=%d",
                event->data.peer_beacon_rx.telemetry.node_id,
                (unsigned long)event->data.peer_beacon_rx.telemetry.packet_seq,
                (long)event->data.peer_beacon_rx.telemetry.position.lat_e7,
                (long)event->data.peer_beacon_rx.telemetry.position.lon_e7,
                (long)event->data.peer_beacon_rx.telemetry.position.alt_mm,
                event->data.peer_beacon_rx.telemetry.gnss_valid ? 1u : 0u,
                event->data.peer_beacon_rx.rssi_dbm,
                event->data.peer_beacon_rx.snr_db
            );
            emit_log(sys, now_ms, NAV_LOG_INFO, NAV_LOG_CAT_PEER_TABLE, "telemetry_update", message);
        }
        update_snapshot(sys, now_ms, false);
        break;
    case NAV_EVT_RANGE_RESULT:
        if (nav_peer_table_update_range(&sys->peer_table, &event->data.range_result, now_ms)) {
            mark_radio_solve_inputs_changed(sys);
            (void)snprintf(
                message,
                sizeof(message),
                "peer=%u request_id=%u range_mm=%lu range_sigma_mm=%lu valid=%u rssi_dbm=%d snr_db=%d",
                event->data.range_result.peer_id,
                event->data.range_result.request_id,
                (unsigned long)event->data.range_result.range_mm,
                (unsigned long)event->data.range_result.range_sigma_mm,
                event->data.range_result.valid ? 1u : 0u,
                event->data.range_result.rssi_dbm,
                event->data.range_result.snr_db
            );
            emit_log(sys, now_ms, NAV_LOG_INFO, NAV_LOG_CAT_RANGE, "range_update", message);
        }
        update_snapshot(sys, now_ms, false);
        break;
    case NAV_EVT_RANGE_FAIL: {
        nav_range_result_t failed = {
            .peer_id = event->data.range_failure.peer_id,
            .request_id = event->data.range_failure.request_id,
            .timestamp_ms = event->data.range_failure.timestamp_ms,
            .valid = false,
        };
        if (!sys->config.retain_last_range_on_failure) {
            (void)nav_peer_table_update_range(&sys->peer_table, &failed, now_ms);
            mark_radio_solve_inputs_changed(sys);
        }
        (void)snprintf(
            message,
            sizeof(message),
            "peer=%u request_id=%u range_fail_reason=%s",
            event->data.range_failure.peer_id,
            event->data.range_failure.request_id,
            nav_range_fail_reason_to_string(event->data.range_failure.reason)
        );
        emit_log(sys, now_ms, NAV_LOG_WARN, NAV_LOG_CAT_RANGE, "range_fail", message);
        update_snapshot(sys, now_ms, false);
        break;
    }
    case NAV_EVT_CONFIG_COMMAND:
        if (event->data.config_command.command == NAV_CONFIG_CMD_FORCE_GPS_DENIED) {
            sys->config.demo_force_gps_denied = event->data.config_command.enabled;
        } else if (event->data.config_command.command == NAV_CONFIG_CMD_USE_GNSS) {
            sys->config.demo_force_gps_denied = false;
        }
        mark_radio_solve_inputs_changed(sys);
        (void)snprintf(message, sizeof(message), "forced_denied=%u", sys->config.demo_force_gps_denied ? 1u : 0u);
        emit_log(sys, now_ms, NAV_LOG_INFO, NAV_LOG_CAT_CONFIG, "config_command", message);
        update_snapshot(sys, now_ms, false);
        break;
    case NAV_EVT_NONE:
    default:
        break;
    }
}

void nav_core_tick(nav_system_t *sys, uint32_t now_ms)
{
    if (sys == NULL) {
        return;
    }
    sys->last_tick_ms = now_ms;
    nav_peer_table_mark_stale(&sys->peer_table, now_ms, sys->config.telemetry_ttl_ms, sys->config.range_ttl_ms);
    for (uint8_t node_id = 0u; node_id < NAV_MAX_NODES; ++node_id) {
        const nav_peer_state_t *peer = nav_peer_table_get_const(&sys->peer_table, node_id);
        if (peer == NULL) {
            continue;
        }
        if (peer->last_reject_reason == NAV_REJECT_STALE_TELEMETRY || peer->last_reject_reason == NAV_REJECT_STALE_RANGE) {
            char message[144];
            (void)snprintf(
                message,
                sizeof(message),
                "peer=%u reason=%s telemetry_age_ms=%lu range_age_ms=%lu",
                node_id,
                nav_reject_reason_to_string(peer->last_reject_reason),
                (unsigned long)(now_ms - peer->last_telemetry_timestamp_ms),
                (unsigned long)(now_ms - peer->last_range_timestamp_ms)
            );
            emit_log(sys, now_ms, NAV_LOG_WARN, NAV_LOG_CAT_PEER_TABLE, "peer_marked_stale", message);
        }
    }
    emit_log(sys, now_ms, NAV_LOG_TRACE, NAV_LOG_CAT_STATE, "tick", "timer tick");
    update_snapshot(sys, now_ms, true);
}

bool nav_core_get_snapshot(const nav_system_t *sys, nav_snapshot_t *out)
{
    if (sys == NULL || out == NULL) {
        return false;
    }
    *out = sys->snapshot;
    return true;
}
