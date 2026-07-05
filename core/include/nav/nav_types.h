#ifndef NAV_TYPES_H
#define NAV_TYPES_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NAV_MAX_NODES 4
#define NAV_INVALID_NODE_ID 0xFFu
#define NAV_TRILAT_ANCHOR_COUNT 3

typedef enum {
    NAV_STATUS_OK = 0,
    NAV_STATUS_INVALID_ARGUMENT = -1,
    NAV_STATUS_NOT_FOUND = -2,
    NAV_STATUS_NO_SPACE = -3,
    NAV_STATUS_BAD_FRAME = -4,
    NAV_STATUS_NOT_IMPLEMENTED = -5,
    NAV_STATUS_INTERNAL_ERROR = -6
} nav_status_t;

typedef enum {
    NAV_MODE_BOOT = 0,
    NAV_MODE_GNSS_ACQUIRE,
    NAV_MODE_GNSS_OK,
    NAV_MODE_GNSS_SUSPECT,
    NAV_MODE_GPS_DENIED,
    NAV_MODE_RADIO_NAV_OK,
    NAV_MODE_RADIO_NAV_DEGRADED,
    NAV_MODE_NO_NAV_SOLUTION,
    NAV_MODE_DEMO_FORCED_DENIED
} nav_mode_t;

typedef enum {
    NAV_SOLUTION_NONE = 0,
    NAV_SOLUTION_GNSS_DIRECT,
    NAV_SOLUTION_RADIO_3D,
    NAV_SOLUTION_DEGRADED,
    NAV_SOLUTION_REJECTED
} nav_solution_status_t;

typedef enum {
    NAV_SOURCE_NONE = 0,
    NAV_SOURCE_LOCAL_GNSS,
    NAV_SOURCE_RADIO_3D,
    NAV_SOURCE_REPLAY,
    NAV_SOURCE_SIM
} nav_solution_source_t;

typedef enum {
    NAV_REJECT_NONE = 0,
    NAV_REJECT_STALE_TELEMETRY,
    NAV_REJECT_STALE_RANGE,
    NAV_REJECT_BAD_GNSS,
    NAV_REJECT_BAD_POSITION,
    NAV_REJECT_BAD_RANGE_SIGMA,
    NAV_REJECT_RANGE_OUTLIER,
    NAV_REJECT_BAD_GEOMETRY,
    NAV_REJECT_NOT_ENOUGH_ANCHORS,
    NAV_REJECT_MISSING_LOCAL_ALTITUDE,
    NAV_REJECT_TRILATERATION_FAILED
} nav_reject_reason_t;

typedef enum {
    NAV_GNSS_FIX_NONE = 0,
    NAV_GNSS_FIX_2D = 2,
    NAV_GNSS_FIX_3D = 3,
    NAV_GNSS_FIX_RTK_FLOAT = 4,
    NAV_GNSS_FIX_RTK_FIXED = 5
} nav_gnss_fix_type_t;

typedef enum {
    NAV_ALT_SOURCE_NONE = 0,
    NAV_ALT_SOURCE_GNSS,
    NAV_ALT_SOURCE_BARO,
    NAV_ALT_SOURCE_FC,
    NAV_ALT_SOURCE_SIM,
    NAV_ALT_SOURCE_MANUAL
} nav_altitude_source_t;

typedef enum {
    NAV_RANGE_FAIL_NONE = 0,
    NAV_RANGE_FAIL_TIMEOUT,
    NAV_RANGE_FAIL_NO_RESPONSE,
    NAV_RANGE_FAIL_RADIO_BUSY,
    NAV_RANGE_FAIL_BAD_FRAME,
    NAV_RANGE_FAIL_RANGING_ENGINE_ERROR,
    NAV_RANGE_FAIL_ABORTED,
    NAV_RANGE_FAIL_UNKNOWN
} nav_range_fail_reason_t;

typedef enum {
    NAV_RADIO_SOLVE_NONE = 0,
    NAV_RADIO_SOLVE_GNSS_DIRECT,
    NAV_RADIO_SOLVE_SOLVED,
    NAV_RADIO_SOLVE_REJECTED,
    NAV_RADIO_SOLVE_SKIPPED_CADENCE,
    NAV_RADIO_SOLVE_SKIPPED_UNCHANGED_INPUTS
} nav_radio_solve_outcome_t;

typedef struct {
    int32_t lat_e7;
    int32_t lon_e7;
    int32_t alt_mm;
} nav_position_t;

typedef struct {
    int32_t vel_n_mmps;
    int32_t vel_e_mmps;
    int32_t vel_d_mmps;
} nav_velocity_t;

typedef struct {
    uint8_t node_id;
    uint32_t timestamp_ms;
    nav_position_t position;
    nav_velocity_t velocity;
    nav_gnss_fix_type_t fix_type;
    bool valid;
    uint8_t satellites;
    uint16_t hdop_centi;
    uint32_t hacc_mm;
    uint32_t vacc_mm;
} nav_gnss_sample_t;

typedef struct {
    int32_t alt_mm;
    uint32_t timestamp_ms;
    nav_altitude_source_t source;
    bool valid;
} nav_local_altitude_t;

typedef struct {
    uint8_t node_id;
    uint32_t packet_seq;
    uint32_t timestamp_ms;
    nav_position_t position;
    nav_velocity_t velocity;
    nav_gnss_fix_type_t fix_type;
    bool gnss_valid;
    uint8_t satellites;
    uint16_t hdop_centi;
    uint32_t hacc_mm;
    uint32_t vacc_mm;
    nav_mode_t nav_mode;
    nav_solution_status_t solution_status;
    nav_solution_source_t solution_source;
} nav_peer_telemetry_t;

typedef struct {
    nav_peer_telemetry_t telemetry;
    int16_t rssi_dbm;
    int16_t snr_db;
} nav_peer_beacon_rx_t;

typedef struct {
    uint8_t peer_id;
    uint16_t request_id;
    uint32_t timestamp_ms;
    uint32_t range_mm;
    uint32_t range_sigma_mm;
    int16_t rssi_dbm;
    int16_t snr_db;
    bool valid;
} nav_range_result_t;

typedef struct {
    uint8_t peer_id;
    uint16_t request_id;
    uint32_t timestamp_ms;
    nav_range_fail_reason_t reason;
} nav_range_failure_t;

typedef struct {
    uint8_t origin_node_id;
    uint16_t ttl_ms;
} nav_debug_enable_t;

typedef struct {
    uint8_t node_id;
    nav_mode_t nav_mode;
    nav_solution_status_t solution_status;
    nav_solution_source_t solution_source;
    uint8_t num_anchors;
    uint8_t anchor_ids[NAV_TRILAT_ANCHOR_COUNT];
    nav_gnss_fix_type_t fix_type;
    uint8_t satellites;
    float geometry_score;
    float total_quality;
    uint32_t residual_rms_mm;
    uint32_t max_residual_mm;
    uint16_t hdop_centi;
    uint32_t hacc_mm;
    uint32_t vacc_mm;
    nav_position_t position;
    uint32_t packet_seq;
} nav_node_quality_report_t;

typedef struct {
    uint8_t node_id;
    int32_t lat_e7;
    int32_t lon_e7;
    int32_t alt_mm;
    uint32_t range_mm;
    uint32_t range_sigma_mm;
    float quality;
    nav_reject_reason_t reject_reason;
} nav_anchor_t;

typedef struct {
    nav_anchor_t anchors[NAV_TRILAT_ANCHOR_COUNT];
    size_t count;
    nav_reject_reason_t reject_reason;
    uint8_t rejected_node_ids[NAV_MAX_NODES];
    nav_reject_reason_t rejected_reasons[NAV_MAX_NODES];
    size_t rejected_count;
} nav_anchor_selection_t;

typedef struct {
    uint8_t node_id;
    uint32_t time_ms;
    nav_mode_t nav_mode;
    nav_solution_status_t solution_status;
    nav_solution_source_t solution_source;
    nav_position_t position;
    uint32_t hacc_mm;
    uint32_t vacc_mm;
    uint8_t num_anchors;
    uint8_t selected_anchor_node_ids[NAV_TRILAT_ANCHOR_COUNT];
    int32_t anchor_residuals_mm[NAV_TRILAT_ANCHOR_COUNT];
    uint8_t rejected_node_ids[NAV_MAX_NODES];
    nav_reject_reason_t rejected_reasons[NAV_MAX_NODES];
    size_t rejected_count;
    float residual_rms_m;
    float max_residual_m;
    float geometry_score;
    float anchor_triangle_area_m2;
    float total_quality;
    nav_reject_reason_t reject_reason;
    nav_altitude_source_t altitude_source;
    bool local_altitude_valid;
    bool local_gnss_present;
    bool local_gnss_valid;
    bool local_gnss_used;
    uint32_t local_gnss_age_ms;
    nav_gnss_fix_type_t local_gnss_fix_type;
    uint8_t local_gnss_satellites;
    uint16_t local_gnss_hdop_centi;
    uint32_t local_gnss_hacc_mm;
    uint32_t local_gnss_vacc_mm;
    nav_position_t local_gnss_position;
    nav_radio_solve_outcome_t radio_solve_outcome;
    uint32_t radio_solve_age_ms;
    uint32_t radio_solve_elapsed_ms;
    uint32_t radio_solve_interval_ms;
    uint32_t radio_solve_generation;
    uint32_t radio_solve_last_generation;
} nav_snapshot_t;

#ifdef __cplusplus
}
#endif

#endif
