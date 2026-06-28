#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "nav/nav_core.h"
#include "nav/nav_quality.h"
#include "nav/nav_state_machine.h"

#define REPLAY_MAX_COLUMNS 32
#define REPLAY_MAX_FIELD_LEN 96
#define REPLAY_MAX_LINE_LEN 2048
#define REPLAY_PATH_LEN 512
#define REPLAY_MAX_TRUTH_ROWS 512
#define REPLAY_EARTH_RADIUS_M 6378137.0

typedef enum {
    COL_TIME_MS = 0,
    COL_EVENT_TYPE,
    COL_NODE_ID,
    COL_PEER_ID,
    COL_PACKET_SEQ,
    COL_REQUEST_ID,
    COL_LAT_E7,
    COL_LON_E7,
    COL_ALT_MM,
    COL_VEL_N_MMPS,
    COL_VEL_E_MMPS,
    COL_VEL_D_MMPS,
    COL_FIX_TYPE,
    COL_GNSS_VALID,
    COL_SATELLITES,
    COL_HDOP_CENTI,
    COL_HACC_MM,
    COL_VACC_MM,
    COL_NAV_MODE,
    COL_RANGE_MM,
    COL_RANGE_SIGMA_MM,
    COL_RSSI_DBM,
    COL_SNR_DB,
    COL_RANGE_VALID,
    COL_ALT_SOURCE,
    COL_ALT_VALID,
    COL_RANGE_FAIL_REASON,
    COL_COUNT
} replay_column_t;

typedef enum {
    REPLAY_EVT_TICK = 0,
    REPLAY_EVT_LOCAL_GNSS_SAMPLE,
    REPLAY_EVT_LOCAL_ALTITUDE_SAMPLE,
    REPLAY_EVT_PEER_BEACON_RX,
    REPLAY_EVT_RANGE_RESULT,
    REPLAY_EVT_RANGE_FAIL
} replay_event_type_t;

typedef struct {
    int index[COL_COUNT];
} replay_header_t;

typedef struct {
    char fields[REPLAY_MAX_COLUMNS][REPLAY_MAX_FIELD_LEN];
    int count;
} replay_row_t;

typedef struct {
    const char *events_path;
    const char *out_dir;
    const char *truth_path;
    const char *config_path;
    uint8_t node_id;
    bool node_id_override;
    bool no_truth;
    bool no_config;
    bool pretty;
} replay_options_t;

typedef struct {
    FILE *logs;
    unsigned events_read;
    unsigned events_applied;
    unsigned solutions_written;
} replay_context_t;

typedef struct {
    uint32_t time_ms;
    uint8_t node_id;
    int32_t true_lat_e7;
    int32_t true_lon_e7;
    int32_t true_alt_mm;
    int32_t true_vn_mmps;
    int32_t true_ve_mmps;
    int32_t true_vd_mmps;
} replay_truth_row_t;

typedef struct {
    replay_truth_row_t rows[REPLAY_MAX_TRUTH_ROWS];
    size_t count;
} replay_truth_table_t;

typedef struct {
    bool has_expected_mode;
    bool has_expected_solution;
    bool has_expected_source;
    bool has_expected_reject;
    nav_mode_t expected_mode;
    nav_solution_status_t expected_solution;
    nav_solution_source_t expected_source;
    nav_reject_reason_t expected_reject;
    double max_allowed_horizontal_error_m;
    double max_allowed_vertical_error_m;
    double max_allowed_3d_error_m;
} replay_config_extra_t;

typedef struct {
    unsigned rows_compared;
    unsigned rows_skipped_no_truth;
    unsigned rows_skipped_no_radio_solution;
    double max_horizontal_error_m;
    double sum_sq_horizontal_error_m;
    double max_vertical_error_m;
    double sum_sq_vertical_error_m;
    double max_3d_error_m;
    double sum_sq_3d_error_m;
} replay_compare_stats_t;

static const char *COLUMN_NAMES[COL_COUNT] = {
    "time_ms",
    "event_type",
    "node_id",
    "peer_id",
    "packet_seq",
    "request_id",
    "lat_e7",
    "lon_e7",
    "alt_mm",
    "vel_n_mmps",
    "vel_e_mmps",
    "vel_d_mmps",
    "fix_type",
    "gnss_valid",
    "satellites",
    "hdop_centi",
    "hacc_mm",
    "vacc_mm",
    "nav_mode",
    "range_mm",
    "range_sigma_mm",
    "rssi_dbm",
    "snr_db",
    "range_valid",
    "alt_source",
    "alt_valid",
    "range_fail_reason",
};

static void log_line(FILE *logs, const char *fmt, ...)
{
    if (logs == NULL) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    vfprintf(logs, fmt, args);
    va_end(args);
    fputc('\n', logs);
    fflush(logs);
}

static void core_log_callback(
    uint32_t timestamp_ms,
    nav_log_level_t level,
    nav_log_category_t category,
    const char *event,
    const char *message,
    void *user
)
{
    FILE *logs = (FILE *)user;
    log_line(
        logs,
        "t=%lu level=%s cat=%s event=%s %s",
        (unsigned long)timestamp_ms,
        nav_log_level_to_string(level),
        nav_log_category_to_string(category),
        event,
        message
    );
}

static void usage(const char *argv0)
{
    fprintf(stderr, "usage: %s --events events.csv --out-dir output_dir [--truth truth.csv] [--config replay_config.csv] [--node-id N] [--no-truth] [--no-config] [--pretty]\n", argv0);
    fprintf(stderr, "   or: %s events.csv output_dir\n", argv0);
}

static bool parse_options(int argc, char **argv, replay_options_t *options)
{
    memset(options, 0, sizeof(*options));
    options->node_id = 0u;

    if (argc == 3 && argv[1][0] != '-') {
        options->events_path = argv[1];
        options->out_dir = argv[2];
        return true;
    }

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--events") == 0 && i + 1 < argc) {
            options->events_path = argv[++i];
        } else if (strcmp(argv[i], "--out-dir") == 0 && i + 1 < argc) {
            options->out_dir = argv[++i];
        } else if (strcmp(argv[i], "--truth") == 0 && i + 1 < argc) {
            options->truth_path = argv[++i];
        } else if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            options->config_path = argv[++i];
        } else if (strcmp(argv[i], "--node-id") == 0 && i + 1 < argc) {
            char *end = NULL;
            long value = strtol(argv[++i], &end, 10);
            if (*end != '\0' || value < 0 || value >= NAV_MAX_NODES) {
                fprintf(stderr, "invalid --node-id: %s\n", argv[i]);
                return false;
            }
            options->node_id = (uint8_t)value;
            options->node_id_override = true;
        } else if (strcmp(argv[i], "--no-truth") == 0) {
            options->no_truth = true;
        } else if (strcmp(argv[i], "--no-config") == 0) {
            options->no_config = true;
        } else if (strcmp(argv[i], "--pretty") == 0) {
            options->pretty = true;
        } else {
            usage(argv[0]);
            return false;
        }
    }

    return options->events_path != NULL && options->out_dir != NULL;
}

static int mkdir_recursive(const char *path)
{
    char tmp[REPLAY_PATH_LEN];
    const size_t len = strlen(path);
    if (len == 0u || len >= sizeof(tmp)) {
        return -1;
    }
    memcpy(tmp, path, len + 1u);
    for (char *p = tmp + 1; *p != '\0'; ++p) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0777) != 0 && errno != EEXIST) {
                return -1;
            }
            *p = '/';
        }
    }
    if (mkdir(tmp, 0777) != 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

static bool join_path(char *out, size_t out_len, const char *dir, const char *name)
{
    return snprintf(out, out_len, "%s/%s", dir, name) > 0 && strlen(out) < out_len;
}

static bool file_exists(const char *path)
{
    struct stat st;
    return path != NULL && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static bool dirname_from_path(char *out, size_t out_len, const char *path)
{
    const char *slash = strrchr(path, '/');
    if (slash == NULL) {
        return snprintf(out, out_len, ".") > 0 && strlen(out) < out_len;
    }
    const size_t len = (size_t)(slash - path);
    if (len == 0u || len >= out_len) {
        return false;
    }
    memcpy(out, path, len);
    out[len] = '\0';
    return true;
}

static char *trim(char *s)
{
    while (isspace((unsigned char)*s)) {
        ++s;
    }
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) {
        --end;
    }
    *end = '\0';
    return s;
}

static int split_csv_line(char *line, replay_row_t *row)
{
    row->count = 0;
    char *start = line;
    for (char *p = line;; ++p) {
        if (*p == ',' || *p == '\n' || *p == '\r' || *p == '\0') {
            const char saved = *p;
            *p = '\0';
            char *field = trim(start);
            if (row->count >= REPLAY_MAX_COLUMNS || strlen(field) >= REPLAY_MAX_FIELD_LEN) {
                return -1;
            }
            strcpy(row->fields[row->count], field);
            ++row->count;
            if (saved == '\0' || saved == '\n' || saved == '\r') {
                return row->count;
            }
            start = p + 1;
        }
    }
}

static bool line_is_ignored(char *line)
{
    char *s = trim(line);
    return *s == '\0' || *s == '#';
}

static const char *field(const replay_header_t *header, const replay_row_t *row, replay_column_t column)
{
    const int index = header->index[column];
    if (index < 0 || index >= row->count) {
        return "";
    }
    return row->fields[index];
}

static bool field_required(
    FILE *logs,
    const char *path,
    unsigned line_no,
    const replay_header_t *header,
    const replay_row_t *row,
    replay_column_t column
)
{
    if (field(header, row, column)[0] != '\0') {
        return true;
    }
    fprintf(stderr, "%s:%u: missing required field: %s\n", path, line_no, COLUMN_NAMES[column]);
    log_line(logs, "%s:%u: missing required field: %s", path, line_no, COLUMN_NAMES[column]);
    return false;
}

static bool parse_i64(const char *text, int64_t min_value, int64_t max_value, int64_t *out)
{
    if (text == NULL || *text == '\0') {
        return false;
    }
    char *end = NULL;
    errno = 0;
    long long value = strtoll(text, &end, 10);
    if (errno != 0 || *end != '\0' || value < min_value || value > max_value) {
        return false;
    }
    *out = (int64_t)value;
    return true;
}

static bool parse_double_text(const char *text, double min_value, double max_value, double *out)
{
    if (text == NULL || *text == '\0') {
        return false;
    }
    char *end = NULL;
    errno = 0;
    const double value = strtod(text, &end);
    if (errno != 0 || *end != '\0' || !isfinite(value) || value < min_value || value > max_value) {
        return false;
    }
    *out = value;
    return true;
}

static bool parse_bool_text(const char *text, bool *out)
{
    if (strcmp(text, "1") == 0 || strcmp(text, "true") == 0 || strcmp(text, "TRUE") == 0) {
        *out = true;
        return true;
    }
    if (strcmp(text, "0") == 0 || strcmp(text, "false") == 0 || strcmp(text, "FALSE") == 0) {
        *out = false;
        return true;
    }
    return false;
}

static bool parse_nav_mode_text(const char *text, nav_mode_t *out)
{
    if (strcmp(text, "BOOT") == 0) *out = NAV_MODE_BOOT;
    else if (strcmp(text, "GNSS_ACQUIRE") == 0) *out = NAV_MODE_GNSS_ACQUIRE;
    else if (strcmp(text, "GNSS_OK") == 0) *out = NAV_MODE_GNSS_OK;
    else if (strcmp(text, "GNSS_SUSPECT") == 0) *out = NAV_MODE_GNSS_SUSPECT;
    else if (strcmp(text, "GPS_DENIED") == 0) *out = NAV_MODE_GPS_DENIED;
    else if (strcmp(text, "RADIO_NAV_OK") == 0) *out = NAV_MODE_RADIO_NAV_OK;
    else if (strcmp(text, "RADIO_NAV_DEGRADED") == 0) *out = NAV_MODE_RADIO_NAV_DEGRADED;
    else if (strcmp(text, "NO_NAV_SOLUTION") == 0) *out = NAV_MODE_NO_NAV_SOLUTION;
    else if (strcmp(text, "DEMO_FORCED_DENIED") == 0) *out = NAV_MODE_DEMO_FORCED_DENIED;
    else return false;
    return true;
}

static bool parse_fix_type_text(const char *text, nav_gnss_fix_type_t *out)
{
    if (strcmp(text, "NONE") == 0 || strcmp(text, "0") == 0) *out = NAV_GNSS_FIX_NONE;
    else if (strcmp(text, "2D") == 0 || strcmp(text, "2") == 0) *out = NAV_GNSS_FIX_2D;
    else if (strcmp(text, "3D") == 0 || strcmp(text, "3") == 0) *out = NAV_GNSS_FIX_3D;
    else if (strcmp(text, "RTK_FLOAT") == 0 || strcmp(text, "4") == 0) *out = NAV_GNSS_FIX_RTK_FLOAT;
    else if (strcmp(text, "RTK_FIXED") == 0 || strcmp(text, "5") == 0) *out = NAV_GNSS_FIX_RTK_FIXED;
    else return false;
    return true;
}

static bool parse_alt_source_text(const char *text, nav_altitude_source_t *out)
{
    if (strcmp(text, "NONE") == 0) *out = NAV_ALT_SOURCE_NONE;
    else if (strcmp(text, "GNSS") == 0) *out = NAV_ALT_SOURCE_GNSS;
    else if (strcmp(text, "BARO") == 0) *out = NAV_ALT_SOURCE_BARO;
    else if (strcmp(text, "FC") == 0) *out = NAV_ALT_SOURCE_FC;
    else if (strcmp(text, "SIM") == 0) *out = NAV_ALT_SOURCE_SIM;
    else if (strcmp(text, "MANUAL") == 0) *out = NAV_ALT_SOURCE_MANUAL;
    else return false;
    return true;
}

static bool parse_range_fail_text(const char *text, nav_range_fail_reason_t *out)
{
    if (strcmp(text, "NONE") == 0) *out = NAV_RANGE_FAIL_NONE;
    else if (strcmp(text, "TIMEOUT") == 0) *out = NAV_RANGE_FAIL_TIMEOUT;
    else if (strcmp(text, "NO_RESPONSE") == 0) *out = NAV_RANGE_FAIL_NO_RESPONSE;
    else if (strcmp(text, "RADIO_BUSY") == 0) *out = NAV_RANGE_FAIL_RADIO_BUSY;
    else if (strcmp(text, "BAD_FRAME") == 0) *out = NAV_RANGE_FAIL_BAD_FRAME;
    else if (strcmp(text, "RANGING_ENGINE_ERROR") == 0) *out = NAV_RANGE_FAIL_RANGING_ENGINE_ERROR;
    else if (strcmp(text, "ABORTED") == 0) *out = NAV_RANGE_FAIL_ABORTED;
    else if (strcmp(text, "UNKNOWN") == 0) *out = NAV_RANGE_FAIL_UNKNOWN;
    else return false;
    return true;
}

static bool parse_solution_status_text(const char *text, nav_solution_status_t *out)
{
    if (strcmp(text, "NONE") == 0) *out = NAV_SOLUTION_NONE;
    else if (strcmp(text, "GNSS_DIRECT") == 0) *out = NAV_SOLUTION_GNSS_DIRECT;
    else if (strcmp(text, "RADIO_3D") == 0) *out = NAV_SOLUTION_RADIO_3D;
    else if (strcmp(text, "DEGRADED") == 0) *out = NAV_SOLUTION_DEGRADED;
    else if (strcmp(text, "REJECTED") == 0) *out = NAV_SOLUTION_REJECTED;
    else return false;
    return true;
}

static bool parse_solution_source_text(const char *text, nav_solution_source_t *out)
{
    if (strcmp(text, "NONE") == 0) *out = NAV_SOURCE_NONE;
    else if (strcmp(text, "LOCAL_GNSS") == 0) *out = NAV_SOURCE_LOCAL_GNSS;
    else if (strcmp(text, "RADIO_3D") == 0) *out = NAV_SOURCE_RADIO_3D;
    else if (strcmp(text, "REPLAY") == 0) *out = NAV_SOURCE_REPLAY;
    else if (strcmp(text, "SIM") == 0) *out = NAV_SOURCE_SIM;
    else return false;
    return true;
}

static bool parse_reject_reason_text(const char *text, nav_reject_reason_t *out)
{
    if (strcmp(text, "NONE") == 0) *out = NAV_REJECT_NONE;
    else if (strcmp(text, "STALE_TELEMETRY") == 0) *out = NAV_REJECT_STALE_TELEMETRY;
    else if (strcmp(text, "STALE_RANGE") == 0) *out = NAV_REJECT_STALE_RANGE;
    else if (strcmp(text, "BAD_GNSS") == 0) *out = NAV_REJECT_BAD_GNSS;
    else if (strcmp(text, "BAD_POSITION") == 0) *out = NAV_REJECT_BAD_POSITION;
    else if (strcmp(text, "BAD_RANGE_SIGMA") == 0) *out = NAV_REJECT_BAD_RANGE_SIGMA;
    else if (strcmp(text, "RANGE_OUTLIER") == 0) *out = NAV_REJECT_RANGE_OUTLIER;
    else if (strcmp(text, "BAD_GEOMETRY") == 0) *out = NAV_REJECT_BAD_GEOMETRY;
    else if (strcmp(text, "NOT_ENOUGH_ANCHORS") == 0) *out = NAV_REJECT_NOT_ENOUGH_ANCHORS;
    else if (strcmp(text, "MISSING_LOCAL_ALTITUDE") == 0) *out = NAV_REJECT_MISSING_LOCAL_ALTITUDE;
    else if (strcmp(text, "TRILATERATION_FAILED") == 0) *out = NAV_REJECT_TRILATERATION_FAILED;
    else return false;
    return true;
}

static bool parse_event_type_text(const char *text, replay_event_type_t *out)
{
    if (strcmp(text, "TICK") == 0) *out = REPLAY_EVT_TICK;
    else if (strcmp(text, "LOCAL_GNSS_SAMPLE") == 0) *out = REPLAY_EVT_LOCAL_GNSS_SAMPLE;
    else if (strcmp(text, "LOCAL_ALTITUDE_SAMPLE") == 0) *out = REPLAY_EVT_LOCAL_ALTITUDE_SAMPLE;
    else if (strcmp(text, "PEER_BEACON_RX") == 0) *out = REPLAY_EVT_PEER_BEACON_RX;
    else if (strcmp(text, "RANGE_RESULT") == 0) *out = REPLAY_EVT_RANGE_RESULT;
    else if (strcmp(text, "RANGE_FAIL") == 0) *out = REPLAY_EVT_RANGE_FAIL;
    else return false;
    return true;
}

static bool parse_required_i64(
    FILE *logs,
    const char *path,
    unsigned line_no,
    const replay_header_t *header,
    const replay_row_t *row,
    replay_column_t column,
    int64_t min_value,
    int64_t max_value,
    int64_t *out
)
{
    if (!field_required(logs, path, line_no, header, row, column)) {
        return false;
    }
    const char *text = field(header, row, column);
    if (!parse_i64(text, min_value, max_value, out)) {
        fprintf(stderr, "%s:%u: invalid %s: \"%s\"\n", path, line_no, COLUMN_NAMES[column], text);
        log_line(logs, "%s:%u: invalid %s: \"%s\"", path, line_no, COLUMN_NAMES[column], text);
        return false;
    }
    return true;
}

static bool parse_optional_i64(
    const replay_header_t *header,
    const replay_row_t *row,
    replay_column_t column,
    int64_t min_value,
    int64_t max_value,
    int64_t default_value,
    int64_t *out
)
{
    const char *text = field(header, row, column);
    if (*text == '\0') {
        *out = default_value;
        return true;
    }
    return parse_i64(text, min_value, max_value, out);
}

static bool position_valid_e7(int32_t lat_e7, int32_t lon_e7)
{
    return lat_e7 >= -900000000 && lat_e7 <= 900000000 && lon_e7 >= -1800000000 && lon_e7 <= 1800000000;
}

static bool parse_header(const replay_row_t *row, replay_header_t *header)
{
    for (int i = 0; i < COL_COUNT; ++i) {
        header->index[i] = -1;
    }
    for (int col = 0; col < row->count; ++col) {
        for (int expected = 0; expected < COL_COUNT; ++expected) {
            if (strcmp(row->fields[col], COLUMN_NAMES[expected]) == 0) {
                header->index[expected] = col;
            }
        }
    }
    for (int expected = 0; expected < COL_COUNT; ++expected) {
        if (header->index[expected] < 0) {
            fprintf(stderr, "events.csv: missing header column: %s\n", COLUMN_NAMES[expected]);
            return false;
        }
    }
    return true;
}

static void replay_config_extra_init(replay_config_extra_t *extra)
{
    memset(extra, 0, sizeof(*extra));
    extra->max_allowed_horizontal_error_m = 1.0;
    extra->max_allowed_vertical_error_m = 1.0;
    extra->max_allowed_3d_error_m = 1.0;
}

static bool parse_config_u8(const char *path, unsigned line_no, const char *key, const char *value, uint8_t max_value, uint8_t *out)
{
    int64_t parsed = 0;
    if (!parse_i64(value, 0, max_value, &parsed)) {
        fprintf(stderr, "%s:%u: invalid %s: \"%s\"\n", path, line_no, key, value);
        return false;
    }
    *out = (uint8_t)parsed;
    return true;
}

static bool parse_config_u32(const char *path, unsigned line_no, const char *key, const char *value, uint32_t *out)
{
    int64_t parsed = 0;
    if (!parse_i64(value, 0, UINT32_MAX, &parsed)) {
        fprintf(stderr, "%s:%u: invalid %s: \"%s\"\n", path, line_no, key, value);
        return false;
    }
    *out = (uint32_t)parsed;
    return true;
}

static bool parse_config_float(const char *path, unsigned line_no, const char *key, const char *value, float *out)
{
    double parsed = 0.0;
    if (!parse_double_text(value, -FLT_MAX, FLT_MAX, &parsed)) {
        fprintf(stderr, "%s:%u: invalid %s: \"%s\"\n", path, line_no, key, value);
        return false;
    }
    *out = (float)parsed;
    return true;
}

static bool parse_config_double_nonnegative(const char *path, unsigned line_no, const char *key, const char *value, double *out)
{
    if (!parse_double_text(value, 0.0, DBL_MAX, out)) {
        fprintf(stderr, "%s:%u: invalid %s: \"%s\"\n", path, line_no, key, value);
        return false;
    }
    return true;
}

static bool parse_config_bool(const char *path, unsigned line_no, const char *key, const char *value, bool *out)
{
    if (!parse_bool_text(value, out)) {
        fprintf(stderr, "%s:%u: invalid %s: \"%s\"\n", path, line_no, key, value);
        return false;
    }
    return true;
}

static bool apply_config_row(
    const char *path,
    unsigned line_no,
    const char *key,
    const char *value,
    nav_config_t *config,
    replay_config_extra_t *extra
)
{
    if (strcmp(key, "node_id") == 0) return parse_config_u8(path, line_no, key, value, NAV_MAX_NODES - 1u, &config->local_node_id);
    if (strcmp(key, "telemetry_ttl_ms") == 0) return parse_config_u32(path, line_no, key, value, &config->telemetry_ttl_ms);
    if (strcmp(key, "range_ttl_ms") == 0) return parse_config_u32(path, line_no, key, value, &config->range_ttl_ms);
    if (strcmp(key, "local_altitude_ttl_ms") == 0) return parse_config_u32(path, line_no, key, value, &config->local_altitude_ttl_ms);
    if (strcmp(key, "tick_period_ms") == 0) return parse_config_u32(path, line_no, key, value, &config->tick_period_ms);
    if (strcmp(key, "max_range_sigma_mm") == 0) return parse_config_u32(path, line_no, key, value, &config->max_range_sigma_mm);
    if (strcmp(key, "min_anchor_quality") == 0) return parse_config_float(path, line_no, key, value, &config->min_anchor_quality);
    if (strcmp(key, "min_solution_quality") == 0) return parse_config_float(path, line_no, key, value, &config->min_solution_quality);
    if (strcmp(key, "max_residual_rms_m") == 0) return parse_config_float(path, line_no, key, value, &config->max_residual_rms_m);
    if (strcmp(key, "max_residual_m") == 0) return parse_config_float(path, line_no, key, value, &config->max_residual_m);
    if (strcmp(key, "min_anchor_triangle_area_m2") == 0) return parse_config_float(path, line_no, key, value, &config->min_anchor_triangle_area_m2);
    if (strcmp(key, "degraded_anchor_triangle_area_m2") == 0) return parse_config_float(path, line_no, key, value, &config->degraded_anchor_triangle_area_m2);
    if (strcmp(key, "demo_force_gps_denied") == 0) return parse_config_bool(path, line_no, key, value, &config->demo_force_gps_denied);
    if (strcmp(key, "allow_gnss_altitude_in_demo_forced_denied") == 0) return parse_config_bool(path, line_no, key, value, &config->allow_gnss_altitude_in_demo_forced_denied);
    if (strcmp(key, "max_allowed_horizontal_error_m") == 0) return parse_config_double_nonnegative(path, line_no, key, value, &extra->max_allowed_horizontal_error_m);
    if (strcmp(key, "max_allowed_vertical_error_m") == 0) return parse_config_double_nonnegative(path, line_no, key, value, &extra->max_allowed_vertical_error_m);
    if (strcmp(key, "max_allowed_3d_error_m") == 0) return parse_config_double_nonnegative(path, line_no, key, value, &extra->max_allowed_3d_error_m);
    if (strcmp(key, "expect_final_mode") == 0) {
        extra->has_expected_mode = true;
        if (!parse_nav_mode_text(value, &extra->expected_mode)) {
            fprintf(stderr, "%s:%u: invalid expect_final_mode: \"%s\"\n", path, line_no, value);
            return false;
        }
        return true;
    }
    if (strcmp(key, "expect_final_solution") == 0) {
        extra->has_expected_solution = true;
        if (!parse_solution_status_text(value, &extra->expected_solution)) {
            fprintf(stderr, "%s:%u: invalid expect_final_solution: \"%s\"\n", path, line_no, value);
            return false;
        }
        return true;
    }
    if (strcmp(key, "expect_final_source") == 0) {
        extra->has_expected_source = true;
        if (!parse_solution_source_text(value, &extra->expected_source)) {
            fprintf(stderr, "%s:%u: invalid expect_final_source: \"%s\"\n", path, line_no, value);
            return false;
        }
        return true;
    }
    if (strcmp(key, "expect_final_reject") == 0) {
        extra->has_expected_reject = true;
        if (!parse_reject_reason_text(value, &extra->expected_reject)) {
            fprintf(stderr, "%s:%u: invalid expect_final_reject: \"%s\"\n", path, line_no, value);
            return false;
        }
        return true;
    }

    fprintf(stderr, "%s:%u: unknown config key: \"%s\"\n", path, line_no, key);
    return false;
}

static bool load_replay_config(const char *path, nav_config_t *config, replay_config_extra_t *extra)
{
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        fprintf(stderr, "failed to open config file: %s\n", path);
        return false;
    }

    char line[REPLAY_MAX_LINE_LEN];
    unsigned line_no = 0u;
    bool header_seen = false;
    while (fgets(line, sizeof(line), file) != NULL) {
        ++line_no;
        char original[REPLAY_MAX_LINE_LEN];
        strncpy(original, line, sizeof(original));
        original[sizeof(original) - 1u] = '\0';
        if (line_is_ignored(line)) {
            continue;
        }
        replay_row_t row;
        if (split_csv_line(original, &row) < 0 || row.count < 2) {
            fprintf(stderr, "%s:%u: malformed config row\n", path, line_no);
            fclose(file);
            return false;
        }
        if (!header_seen) {
            if (strcmp(row.fields[0], "key") != 0 || strcmp(row.fields[1], "value") != 0) {
                fprintf(stderr, "%s:%u: expected config header: key,value\n", path, line_no);
                fclose(file);
                return false;
            }
            header_seen = true;
            continue;
        }
        if (row.fields[0][0] == '\0' || row.fields[1][0] == '\0') {
            fprintf(stderr, "%s:%u: config key and value are required\n", path, line_no);
            fclose(file);
            return false;
        }
        if (!apply_config_row(path, line_no, row.fields[0], row.fields[1], config, extra)) {
            fclose(file);
            return false;
        }
    }
    fclose(file);
    if (!header_seen) {
        fprintf(stderr, "%s: missing config header row\n", path);
        return false;
    }
    return true;
}

static bool parse_truth_header(const replay_row_t *row, int indexes[8])
{
    static const char *names[8] = {
        "time_ms",
        "node_id",
        "true_lat_e7",
        "true_lon_e7",
        "true_alt_mm",
        "true_vn_mmps",
        "true_ve_mmps",
        "true_vd_mmps",
    };
    for (size_t i = 0u; i < 8u; ++i) {
        indexes[i] = -1;
    }
    for (int col = 0; col < row->count; ++col) {
        for (size_t expected = 0u; expected < 8u; ++expected) {
            if (strcmp(row->fields[col], names[expected]) == 0) {
                indexes[expected] = col;
            }
        }
    }
    for (size_t expected = 0u; expected < 8u; ++expected) {
        if (indexes[expected] < 0) {
            fprintf(stderr, "truth.csv: missing header column: %s\n", names[expected]);
            return false;
        }
    }
    return true;
}

static const char *truth_field(const replay_row_t *row, const int indexes[8], size_t column)
{
    const int index = indexes[column];
    if (index < 0 || index >= row->count) {
        return "";
    }
    return row->fields[index];
}

static bool parse_truth_required_i64(
    const char *path,
    unsigned line_no,
    const replay_row_t *row,
    const int indexes[8],
    size_t column,
    int64_t min_value,
    int64_t max_value,
    int64_t *out
)
{
    static const char *names[8] = {
        "time_ms",
        "node_id",
        "true_lat_e7",
        "true_lon_e7",
        "true_alt_mm",
        "true_vn_mmps",
        "true_ve_mmps",
        "true_vd_mmps",
    };
    const char *text = truth_field(row, indexes, column);
    if (text[0] == '\0') {
        fprintf(stderr, "%s:%u: missing required field: %s\n", path, line_no, names[column]);
        return false;
    }
    if (!parse_i64(text, min_value, max_value, out)) {
        fprintf(stderr, "%s:%u: invalid %s: \"%s\"\n", path, line_no, names[column], text);
        return false;
    }
    return true;
}

static bool parse_truth_optional_i64(
    const replay_row_t *row,
    const int indexes[8],
    size_t column,
    int64_t min_value,
    int64_t max_value,
    int64_t default_value,
    int64_t *out
)
{
    const char *text = truth_field(row, indexes, column);
    if (text[0] == '\0') {
        *out = default_value;
        return true;
    }
    return parse_i64(text, min_value, max_value, out);
}

static bool load_truth_csv(const char *path, replay_truth_table_t *truth)
{
    memset(truth, 0, sizeof(*truth));
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        fprintf(stderr, "failed to open truth file: %s\n", path);
        return false;
    }

    char line[REPLAY_MAX_LINE_LEN];
    unsigned line_no = 0u;
    bool header_seen = false;
    int indexes[8];
    while (fgets(line, sizeof(line), file) != NULL) {
        ++line_no;
        char original[REPLAY_MAX_LINE_LEN];
        strncpy(original, line, sizeof(original));
        original[sizeof(original) - 1u] = '\0';
        if (line_is_ignored(line)) {
            continue;
        }
        replay_row_t row;
        if (split_csv_line(original, &row) < 0) {
            fprintf(stderr, "%s:%u: malformed truth CSV line\n", path, line_no);
            fclose(file);
            return false;
        }
        if (!header_seen) {
            if (!parse_truth_header(&row, indexes)) {
                fclose(file);
                return false;
            }
            header_seen = true;
            continue;
        }
        if (truth->count >= REPLAY_MAX_TRUTH_ROWS) {
            fprintf(stderr, "%s:%u: too many truth rows\n", path, line_no);
            fclose(file);
            return false;
        }
        int64_t value = 0;
        replay_truth_row_t *out = &truth->rows[truth->count];
        if (!parse_truth_required_i64(path, line_no, &row, indexes, 0u, 0, UINT32_MAX, &value)) {
            fclose(file);
            return false;
        }
        out->time_ms = (uint32_t)value;
        if (!parse_truth_required_i64(path, line_no, &row, indexes, 1u, 0, NAV_MAX_NODES - 1, &value)) {
            fclose(file);
            return false;
        }
        out->node_id = (uint8_t)value;
        if (!parse_truth_required_i64(path, line_no, &row, indexes, 2u, INT32_MIN, INT32_MAX, &value)) {
            fclose(file);
            return false;
        }
        out->true_lat_e7 = (int32_t)value;
        if (!parse_truth_required_i64(path, line_no, &row, indexes, 3u, INT32_MIN, INT32_MAX, &value)) {
            fclose(file);
            return false;
        }
        out->true_lon_e7 = (int32_t)value;
        if (!position_valid_e7(out->true_lat_e7, out->true_lon_e7)) {
            fprintf(stderr, "%s:%u: invalid truth lat/lon\n", path, line_no);
            fclose(file);
            return false;
        }
        if (!parse_truth_required_i64(path, line_no, &row, indexes, 4u, INT32_MIN, INT32_MAX, &value)) {
            fclose(file);
            return false;
        }
        out->true_alt_mm = (int32_t)value;
        if (!parse_truth_optional_i64(&row, indexes, 5u, INT32_MIN, INT32_MAX, 0, &value)) {
            fprintf(stderr, "%s:%u: invalid true_vn_mmps: \"%s\"\n", path, line_no, truth_field(&row, indexes, 5u));
            fclose(file);
            return false;
        }
        out->true_vn_mmps = (int32_t)value;
        if (!parse_truth_optional_i64(&row, indexes, 6u, INT32_MIN, INT32_MAX, 0, &value)) {
            fprintf(stderr, "%s:%u: invalid true_ve_mmps: \"%s\"\n", path, line_no, truth_field(&row, indexes, 6u));
            fclose(file);
            return false;
        }
        out->true_ve_mmps = (int32_t)value;
        if (!parse_truth_optional_i64(&row, indexes, 7u, INT32_MIN, INT32_MAX, 0, &value)) {
            fprintf(stderr, "%s:%u: invalid true_vd_mmps: \"%s\"\n", path, line_no, truth_field(&row, indexes, 7u));
            fclose(file);
            return false;
        }
        out->true_vd_mmps = (int32_t)value;
        ++truth->count;
    }
    fclose(file);
    if (!header_seen) {
        fprintf(stderr, "%s: missing truth header row\n", path);
        return false;
    }
    return true;
}

static const replay_truth_row_t *find_truth_row(const replay_truth_table_t *truth, uint32_t time_ms, uint8_t node_id)
{
    for (size_t i = 0u; i < truth->count; ++i) {
        if (truth->rows[i].time_ms == time_ms && truth->rows[i].node_id == node_id) {
            return &truth->rows[i];
        }
    }
    return NULL;
}

static double deg_to_rad(double deg)
{
    return deg * 0.01745329251994329576923690768489;
}

static void compare_snapshot_with_truth(
    const nav_snapshot_t *snapshot,
    const replay_truth_table_t *truth,
    replay_compare_stats_t *stats
)
{
    if (snapshot->solution_status != NAV_SOLUTION_RADIO_3D || snapshot->solution_source != NAV_SOURCE_RADIO_3D) {
        ++stats->rows_skipped_no_radio_solution;
        return;
    }
    const replay_truth_row_t *truth_row = find_truth_row(truth, snapshot->time_ms, snapshot->node_id);
    if (truth_row == NULL) {
        ++stats->rows_skipped_no_truth;
        return;
    }

    const double lat_deg = (double)snapshot->position.lat_e7 / 10000000.0;
    const double lon_deg = (double)snapshot->position.lon_e7 / 10000000.0;
    const double truth_lat_deg = (double)truth_row->true_lat_e7 / 10000000.0;
    const double truth_lon_deg = (double)truth_row->true_lon_e7 / 10000000.0;
    const double mean_lat_rad = deg_to_rad((lat_deg + truth_lat_deg) * 0.5);
    const double dx_m = deg_to_rad(lon_deg - truth_lon_deg) * cos(mean_lat_rad) * REPLAY_EARTH_RADIUS_M;
    const double dy_m = deg_to_rad(lat_deg - truth_lat_deg) * REPLAY_EARTH_RADIUS_M;
    const double horizontal_m = sqrt(dx_m * dx_m + dy_m * dy_m);
    const double vertical_m = fabs(((double)snapshot->position.alt_mm - (double)truth_row->true_alt_mm) / 1000.0);
    const double error_3d_m = sqrt(horizontal_m * horizontal_m + vertical_m * vertical_m);

    ++stats->rows_compared;
    stats->sum_sq_horizontal_error_m += horizontal_m * horizontal_m;
    stats->sum_sq_vertical_error_m += vertical_m * vertical_m;
    stats->sum_sq_3d_error_m += error_3d_m * error_3d_m;
    if (horizontal_m > stats->max_horizontal_error_m) stats->max_horizontal_error_m = horizontal_m;
    if (vertical_m > stats->max_vertical_error_m) stats->max_vertical_error_m = vertical_m;
    if (error_3d_m > stats->max_3d_error_m) stats->max_3d_error_m = error_3d_m;
}

static double rms_from_stats(double sum_sq, unsigned count)
{
    return count == 0u ? 0.0 : sqrt(sum_sq / (double)count);
}

static bool compare_stats_pass(const replay_compare_stats_t *stats, const replay_config_extra_t *extra)
{
    return stats->rows_compared > 0u &&
           stats->max_horizontal_error_m <= extra->max_allowed_horizontal_error_m &&
           stats->max_vertical_error_m <= extra->max_allowed_vertical_error_m &&
           stats->max_3d_error_m <= extra->max_allowed_3d_error_m;
}

static bool write_compare_reports(
    const char *txt_path,
    const char *json_path,
    const replay_compare_stats_t *stats,
    const replay_config_extra_t *extra
)
{
    const bool passed = compare_stats_pass(stats, extra);
    const double rms_horizontal = rms_from_stats(stats->sum_sq_horizontal_error_m, stats->rows_compared);
    const double rms_vertical = rms_from_stats(stats->sum_sq_vertical_error_m, stats->rows_compared);
    const double rms_3d = rms_from_stats(stats->sum_sq_3d_error_m, stats->rows_compared);

    FILE *txt = fopen(txt_path, "w");
    if (txt == NULL) {
        fprintf(stderr, "failed to open compare report: %s\n", txt_path);
        return false;
    }
    fprintf(txt, "rows_compared=%u\n", stats->rows_compared);
    fprintf(txt, "rows_skipped_no_truth=%u\n", stats->rows_skipped_no_truth);
    fprintf(txt, "rows_skipped_no_radio_solution=%u\n", stats->rows_skipped_no_radio_solution);
    fprintf(txt, "max_horizontal_error_m=%.6f\n", stats->max_horizontal_error_m);
    fprintf(txt, "rms_horizontal_error_m=%.6f\n", rms_horizontal);
    fprintf(txt, "max_vertical_error_m=%.6f\n", stats->max_vertical_error_m);
    fprintf(txt, "rms_vertical_error_m=%.6f\n", rms_vertical);
    fprintf(txt, "max_3d_error_m=%.6f\n", stats->max_3d_error_m);
    fprintf(txt, "rms_3d_error_m=%.6f\n", rms_3d);
    fprintf(txt, "max_allowed_horizontal_error_m=%.6f\n", extra->max_allowed_horizontal_error_m);
    fprintf(txt, "max_allowed_vertical_error_m=%.6f\n", extra->max_allowed_vertical_error_m);
    fprintf(txt, "max_allowed_3d_error_m=%.6f\n", extra->max_allowed_3d_error_m);
    fprintf(txt, "pass=%s\n", passed ? "true" : "false");
    fclose(txt);

    FILE *json = fopen(json_path, "w");
    if (json == NULL) {
        fprintf(stderr, "failed to open compare report: %s\n", json_path);
        return false;
    }
    fprintf(json, "{\n");
    fprintf(json, "  \"rows_compared\": %u,\n", stats->rows_compared);
    fprintf(json, "  \"rows_skipped_no_truth\": %u,\n", stats->rows_skipped_no_truth);
    fprintf(json, "  \"rows_skipped_no_radio_solution\": %u,\n", stats->rows_skipped_no_radio_solution);
    fprintf(json, "  \"max_horizontal_error_m\": %.9f,\n", stats->max_horizontal_error_m);
    fprintf(json, "  \"rms_horizontal_error_m\": %.9f,\n", rms_horizontal);
    fprintf(json, "  \"max_vertical_error_m\": %.9f,\n", stats->max_vertical_error_m);
    fprintf(json, "  \"rms_vertical_error_m\": %.9f,\n", rms_vertical);
    fprintf(json, "  \"max_3d_error_m\": %.9f,\n", stats->max_3d_error_m);
    fprintf(json, "  \"rms_3d_error_m\": %.9f,\n", rms_3d);
    fprintf(json, "  \"max_allowed_horizontal_error_m\": %.9f,\n", extra->max_allowed_horizontal_error_m);
    fprintf(json, "  \"max_allowed_vertical_error_m\": %.9f,\n", extra->max_allowed_vertical_error_m);
    fprintf(json, "  \"max_allowed_3d_error_m\": %.9f,\n", extra->max_allowed_3d_error_m);
    fprintf(json, "  \"pass\": %s\n", passed ? "true" : "false");
    fprintf(json, "}\n");
    fclose(json);
    return true;
}

static bool build_event(
    FILE *logs,
    const char *path,
    unsigned line_no,
    const replay_header_t *header,
    const replay_row_t *row,
    replay_event_type_t *replay_type,
    nav_event_t *event
)
{
    memset(event, 0, sizeof(*event));
    int64_t value = 0;
    if (!parse_required_i64(logs, path, line_no, header, row, COL_TIME_MS, 0, UINT32_MAX, &value)) {
        return false;
    }
    event->timestamp_ms = (uint32_t)value;

    if (!field_required(logs, path, line_no, header, row, COL_EVENT_TYPE)) {
        return false;
    }
    const char *event_type = field(header, row, COL_EVENT_TYPE);
    if (!parse_event_type_text(event_type, replay_type)) {
        fprintf(stderr, "%s:%u: unknown event_type: \"%s\"\n", path, line_no, event_type);
        log_line(logs, "%s:%u: unknown event_type: \"%s\"", path, line_no, event_type);
        return false;
    }

    switch (*replay_type) {
    case REPLAY_EVT_TICK:
        event->type = NAV_EVT_TICK;
        return true;

    case REPLAY_EVT_LOCAL_GNSS_SAMPLE: {
        event->type = NAV_EVT_LOCAL_GNSS_SAMPLE;
        nav_gnss_sample_t *gnss = &event->data.local_gnss;
        gnss->timestamp_ms = event->timestamp_ms;
        if (!parse_required_i64(logs, path, line_no, header, row, COL_NODE_ID, 0, NAV_MAX_NODES - 1, &value)) return false;
        gnss->node_id = (uint8_t)value;
        if (!parse_required_i64(logs, path, line_no, header, row, COL_LAT_E7, INT32_MIN, INT32_MAX, &value)) return false;
        gnss->position.lat_e7 = (int32_t)value;
        if (!parse_required_i64(logs, path, line_no, header, row, COL_LON_E7, INT32_MIN, INT32_MAX, &value)) return false;
        gnss->position.lon_e7 = (int32_t)value;
        if (!parse_required_i64(logs, path, line_no, header, row, COL_ALT_MM, INT32_MIN, INT32_MAX, &value)) return false;
        gnss->position.alt_mm = (int32_t)value;
        if (!parse_optional_i64(header, row, COL_VEL_N_MMPS, INT32_MIN, INT32_MAX, 0, &value)) return false;
        gnss->velocity.vel_n_mmps = (int32_t)value;
        if (!parse_optional_i64(header, row, COL_VEL_E_MMPS, INT32_MIN, INT32_MAX, 0, &value)) return false;
        gnss->velocity.vel_e_mmps = (int32_t)value;
        if (!parse_optional_i64(header, row, COL_VEL_D_MMPS, INT32_MIN, INT32_MAX, 0, &value)) return false;
        gnss->velocity.vel_d_mmps = (int32_t)value;
        if (!field_required(logs, path, line_no, header, row, COL_FIX_TYPE)) return false;
        if (!parse_fix_type_text(field(header, row, COL_FIX_TYPE), &gnss->fix_type)) {
            fprintf(stderr, "%s:%u: invalid fix_type: \"%s\"\n", path, line_no, field(header, row, COL_FIX_TYPE));
            return false;
        }
        if (!field_required(logs, path, line_no, header, row, COL_GNSS_VALID)) return false;
        if (!parse_bool_text(field(header, row, COL_GNSS_VALID), &gnss->valid)) return false;
        if (!parse_required_i64(logs, path, line_no, header, row, COL_SATELLITES, 0, UINT8_MAX, &value)) return false;
        gnss->satellites = (uint8_t)value;
        if (!parse_required_i64(logs, path, line_no, header, row, COL_HDOP_CENTI, 0, UINT16_MAX, &value)) return false;
        gnss->hdop_centi = (uint16_t)value;
        if (!parse_required_i64(logs, path, line_no, header, row, COL_HACC_MM, 0, UINT32_MAX, &value)) return false;
        gnss->hacc_mm = (uint32_t)value;
        if (!parse_required_i64(logs, path, line_no, header, row, COL_VACC_MM, 0, UINT32_MAX, &value)) return false;
        gnss->vacc_mm = (uint32_t)value;
        return true;
    }

    case REPLAY_EVT_LOCAL_ALTITUDE_SAMPLE: {
        event->type = NAV_EVT_LOCAL_ALTITUDE_SAMPLE;
        nav_local_altitude_t *alt = &event->data.local_altitude;
        alt->timestamp_ms = event->timestamp_ms;
        if (!parse_required_i64(logs, path, line_no, header, row, COL_ALT_MM, INT32_MIN, INT32_MAX, &value)) return false;
        alt->alt_mm = (int32_t)value;
        if (!field_required(logs, path, line_no, header, row, COL_ALT_SOURCE)) return false;
        if (!parse_alt_source_text(field(header, row, COL_ALT_SOURCE), &alt->source)) {
            fprintf(stderr, "%s:%u: invalid alt_source: \"%s\"\n", path, line_no, field(header, row, COL_ALT_SOURCE));
            return false;
        }
        if (!field_required(logs, path, line_no, header, row, COL_ALT_VALID)) return false;
        if (!parse_bool_text(field(header, row, COL_ALT_VALID), &alt->valid)) return false;
        return true;
    }

    case REPLAY_EVT_PEER_BEACON_RX: {
        event->type = NAV_EVT_PEER_TELEMETRY_RX;
        nav_peer_beacon_rx_t *beacon = &event->data.peer_beacon_rx;
        nav_peer_telemetry_t *telemetry = &beacon->telemetry;
        telemetry->timestamp_ms = event->timestamp_ms;
        if (!parse_required_i64(logs, path, line_no, header, row, COL_PEER_ID, 0, NAV_MAX_NODES - 1, &value)) return false;
        telemetry->node_id = (uint8_t)value;
        if (!parse_required_i64(logs, path, line_no, header, row, COL_PACKET_SEQ, 0, UINT32_MAX, &value)) return false;
        telemetry->packet_seq = (uint32_t)value;
        if (!parse_required_i64(logs, path, line_no, header, row, COL_LAT_E7, INT32_MIN, INT32_MAX, &value)) return false;
        telemetry->position.lat_e7 = (int32_t)value;
        if (!parse_required_i64(logs, path, line_no, header, row, COL_LON_E7, INT32_MIN, INT32_MAX, &value)) return false;
        telemetry->position.lon_e7 = (int32_t)value;
        if (!parse_required_i64(logs, path, line_no, header, row, COL_ALT_MM, INT32_MIN, INT32_MAX, &value)) return false;
        telemetry->position.alt_mm = (int32_t)value;
        if (!parse_optional_i64(header, row, COL_VEL_N_MMPS, INT32_MIN, INT32_MAX, 0, &value)) return false;
        telemetry->velocity.vel_n_mmps = (int32_t)value;
        if (!parse_optional_i64(header, row, COL_VEL_E_MMPS, INT32_MIN, INT32_MAX, 0, &value)) return false;
        telemetry->velocity.vel_e_mmps = (int32_t)value;
        if (!parse_optional_i64(header, row, COL_VEL_D_MMPS, INT32_MIN, INT32_MAX, 0, &value)) return false;
        telemetry->velocity.vel_d_mmps = (int32_t)value;
        if (!field_required(logs, path, line_no, header, row, COL_FIX_TYPE)) return false;
        if (!parse_fix_type_text(field(header, row, COL_FIX_TYPE), &telemetry->fix_type)) {
            fprintf(stderr, "%s:%u: invalid fix_type: \"%s\"\n", path, line_no, field(header, row, COL_FIX_TYPE));
            return false;
        }
        if (!field_required(logs, path, line_no, header, row, COL_GNSS_VALID)) return false;
        if (!parse_bool_text(field(header, row, COL_GNSS_VALID), &telemetry->gnss_valid)) return false;
        if (telemetry->gnss_valid && !position_valid_e7(telemetry->position.lat_e7, telemetry->position.lon_e7)) {
            fprintf(stderr, "%s:%u: invalid peer lat/lon for valid GNSS\n", path, line_no);
            log_line(logs, "%s:%u: invalid peer lat/lon for valid GNSS", path, line_no);
            return false;
        }
        if (!parse_required_i64(logs, path, line_no, header, row, COL_SATELLITES, 0, UINT8_MAX, &value)) return false;
        telemetry->satellites = (uint8_t)value;
        if (!parse_required_i64(logs, path, line_no, header, row, COL_HDOP_CENTI, 0, UINT16_MAX, &value)) return false;
        telemetry->hdop_centi = (uint16_t)value;
        if (!parse_required_i64(logs, path, line_no, header, row, COL_HACC_MM, 0, UINT32_MAX, &value)) return false;
        telemetry->hacc_mm = (uint32_t)value;
        if (!parse_required_i64(logs, path, line_no, header, row, COL_VACC_MM, 0, UINT32_MAX, &value)) return false;
        telemetry->vacc_mm = (uint32_t)value;
        if (!field_required(logs, path, line_no, header, row, COL_NAV_MODE)) return false;
        if (!parse_nav_mode_text(field(header, row, COL_NAV_MODE), &telemetry->nav_mode)) {
            fprintf(stderr, "%s:%u: invalid nav_mode: \"%s\"\n", path, line_no, field(header, row, COL_NAV_MODE));
            return false;
        }
        if (!parse_required_i64(logs, path, line_no, header, row, COL_RSSI_DBM, INT16_MIN, INT16_MAX, &value)) return false;
        beacon->rssi_dbm = (int16_t)value;
        if (!parse_required_i64(logs, path, line_no, header, row, COL_SNR_DB, INT16_MIN, INT16_MAX, &value)) return false;
        beacon->snr_db = (int16_t)value;
        return true;
    }

    case REPLAY_EVT_RANGE_RESULT: {
        event->type = NAV_EVT_RANGE_RESULT;
        nav_range_result_t *range = &event->data.range_result;
        range->timestamp_ms = event->timestamp_ms;
        if (!parse_required_i64(logs, path, line_no, header, row, COL_PEER_ID, 0, NAV_MAX_NODES - 1, &value)) return false;
        range->peer_id = (uint8_t)value;
        if (!parse_required_i64(logs, path, line_no, header, row, COL_REQUEST_ID, 0, UINT16_MAX, &value)) return false;
        range->request_id = (uint16_t)value;
        if (!parse_required_i64(logs, path, line_no, header, row, COL_RANGE_MM, 0, UINT32_MAX, &value)) return false;
        range->range_mm = (uint32_t)value;
        if (!parse_required_i64(logs, path, line_no, header, row, COL_RANGE_SIGMA_MM, 0, UINT32_MAX, &value)) return false;
        range->range_sigma_mm = (uint32_t)value;
        if (!parse_required_i64(logs, path, line_no, header, row, COL_RSSI_DBM, INT16_MIN, INT16_MAX, &value)) return false;
        range->rssi_dbm = (int16_t)value;
        if (!parse_required_i64(logs, path, line_no, header, row, COL_SNR_DB, INT16_MIN, INT16_MAX, &value)) return false;
        range->snr_db = (int16_t)value;
        if (!field_required(logs, path, line_no, header, row, COL_RANGE_VALID)) return false;
        if (!parse_bool_text(field(header, row, COL_RANGE_VALID), &range->valid)) return false;
        return true;
    }

    case REPLAY_EVT_RANGE_FAIL: {
        event->type = NAV_EVT_RANGE_FAIL;
        nav_range_failure_t *failure = &event->data.range_failure;
        failure->timestamp_ms = event->timestamp_ms;
        if (!parse_required_i64(logs, path, line_no, header, row, COL_PEER_ID, 0, NAV_MAX_NODES - 1, &value)) return false;
        failure->peer_id = (uint8_t)value;
        if (!parse_required_i64(logs, path, line_no, header, row, COL_REQUEST_ID, 0, UINT16_MAX, &value)) return false;
        failure->request_id = (uint16_t)value;
        if (!field_required(logs, path, line_no, header, row, COL_RANGE_FAIL_REASON)) return false;
        if (!parse_range_fail_text(field(header, row, COL_RANGE_FAIL_REASON), &failure->reason)) {
            fprintf(stderr, "%s:%u: invalid range_fail_reason: \"%s\"\n", path, line_no, field(header, row, COL_RANGE_FAIL_REASON));
            return false;
        }
        return true;
    }
    }
    return false;
}

static void write_solution_header(FILE *out)
{
    fputs(
        "time_ms,node_id,nav_mode,solution_status,solution_source,lat_e7,lon_e7,alt_mm,hacc_mm,vacc_mm,num_anchors,"
        "anchor0_id,anchor1_id,anchor2_id,residual0_mm,residual1_mm,residual2_mm,residual_rms_m,max_residual_m,"
        "geometry_score,anchor_triangle_area_m2,total_quality,reject_reason,altitude_source,local_altitude_valid\n",
        out
    );
}

static void write_solution_row(FILE *out, const nav_snapshot_t *snapshot)
{
    fprintf(
        out,
        "%lu,%u,%s,%s,%s,%ld,%ld,%ld,%lu,%lu,%u,%u,%u,%u,%ld,%ld,%ld,%.6f,%.6f,%.6f,%.3f,%.6f,%s,%s,%u\n",
        (unsigned long)snapshot->time_ms,
        snapshot->node_id,
        nav_mode_to_string(snapshot->nav_mode),
        nav_solution_status_to_string(snapshot->solution_status),
        nav_solution_source_to_string(snapshot->solution_source),
        (long)snapshot->position.lat_e7,
        (long)snapshot->position.lon_e7,
        (long)snapshot->position.alt_mm,
        (unsigned long)snapshot->hacc_mm,
        (unsigned long)snapshot->vacc_mm,
        snapshot->num_anchors,
        snapshot->selected_anchor_node_ids[0],
        snapshot->selected_anchor_node_ids[1],
        snapshot->selected_anchor_node_ids[2],
        (long)snapshot->anchor_residuals_mm[0],
        (long)snapshot->anchor_residuals_mm[1],
        (long)snapshot->anchor_residuals_mm[2],
        (double)snapshot->residual_rms_m,
        (double)snapshot->max_residual_m,
        (double)snapshot->geometry_score,
        (double)snapshot->anchor_triangle_area_m2,
        (double)snapshot->total_quality,
        nav_reject_reason_to_string(snapshot->reject_reason),
        nav_altitude_source_to_string(snapshot->altitude_source),
        snapshot->local_altitude_valid ? 1u : 0u
    );
}

static void write_peers_header(FILE *out)
{
    fputs(
        "time_ms,local_node_id,peer_id,present,telemetry_age_ms,range_age_ms,packet_seq,request_id,lat_e7,lon_e7,alt_mm,"
        "fix_type,gnss_valid,nav_mode,range_mm,range_sigma_mm,range_valid,rssi_dbm,snr_db,telemetry_quality,range_quality,"
        "anchor_quality,last_residual_m,reject_reason\n",
        out
    );
}

static const char *fix_type_to_string(nav_gnss_fix_type_t fix)
{
    switch (fix) {
    case NAV_GNSS_FIX_NONE:
        return "NONE";
    case NAV_GNSS_FIX_2D:
        return "2D";
    case NAV_GNSS_FIX_3D:
        return "3D";
    case NAV_GNSS_FIX_RTK_FLOAT:
        return "RTK_FLOAT";
    case NAV_GNSS_FIX_RTK_FIXED:
        return "RTK_FIXED";
    default:
        return "UNKNOWN";
    }
}

static void write_peers_rows(FILE *out, const nav_system_t *sys, uint32_t now_ms)
{
    for (uint8_t peer_id = 0u; peer_id < NAV_MAX_NODES; ++peer_id) {
        const nav_peer_state_t *peer = nav_peer_table_get_const(&sys->peer_table, peer_id);
        if (peer == NULL) {
            continue;
        }
        fprintf(
            out,
            "%lu,%u,%u,1,%lu,%lu,%lu,%u,%ld,%ld,%ld,%s,%u,%s,%lu,%lu,%u,%d,%d,%.6f,%.6f,%.6f,%.6f,%s\n",
            (unsigned long)now_ms,
            sys->config.local_node_id,
            peer_id,
            (unsigned long)(now_ms - peer->last_telemetry_timestamp_ms),
            (unsigned long)(now_ms - peer->last_range_timestamp_ms),
            (unsigned long)peer->packet_seq,
            peer->last_range_request_id,
            (long)peer->position.lat_e7,
            (long)peer->position.lon_e7,
            (long)peer->position.alt_mm,
            fix_type_to_string(peer->fix_type),
            peer->gnss_valid ? 1u : 0u,
            nav_mode_to_string(peer->peer_nav_mode),
            (unsigned long)peer->range_mm,
            (unsigned long)peer->range_sigma_mm,
            peer->range_valid ? 1u : 0u,
            peer->rssi_dbm,
            peer->snr_db,
            (double)peer->telemetry_quality,
            (double)peer->range_quality,
            (double)peer->anchor_quality,
            (double)peer->last_residual_m,
            nav_reject_reason_to_string(peer->last_reject_reason)
        );
    }
}

static bool emit_outputs(
    FILE *solution,
    FILE *peers,
    const nav_system_t *sys,
    uint32_t now_ms,
    replay_context_t *ctx,
    const replay_truth_table_t *truth,
    replay_compare_stats_t *compare_stats
)
{
    nav_snapshot_t snapshot;
    if (!nav_core_get_snapshot(sys, &snapshot)) {
        return false;
    }
    write_solution_row(solution, &snapshot);
    write_peers_rows(peers, sys, now_ms);
    if (truth != NULL && compare_stats != NULL) {
        compare_snapshot_with_truth(&snapshot, truth, compare_stats);
    }
    ++ctx->solutions_written;
    return true;
}

static bool validate_expected_final(
    const replay_config_extra_t *extra,
    const nav_snapshot_t *snapshot,
    FILE *logs
)
{
    bool ok = true;
    if (extra->has_expected_mode && snapshot->nav_mode != extra->expected_mode) {
        fprintf(stderr, "expected final mode %s, got %s\n", nav_mode_to_string(extra->expected_mode), nav_mode_to_string(snapshot->nav_mode));
        log_line(logs, "t=%lu level=ERROR cat=REPLAY event=expected_final_mismatch field=mode expected=%s actual=%s",
                 (unsigned long)snapshot->time_ms, nav_mode_to_string(extra->expected_mode), nav_mode_to_string(snapshot->nav_mode));
        ok = false;
    }
    if (extra->has_expected_solution && snapshot->solution_status != extra->expected_solution) {
        fprintf(stderr, "expected final solution %s, got %s\n", nav_solution_status_to_string(extra->expected_solution), nav_solution_status_to_string(snapshot->solution_status));
        log_line(logs, "t=%lu level=ERROR cat=REPLAY event=expected_final_mismatch field=solution expected=%s actual=%s",
                 (unsigned long)snapshot->time_ms, nav_solution_status_to_string(extra->expected_solution), nav_solution_status_to_string(snapshot->solution_status));
        ok = false;
    }
    if (extra->has_expected_source && snapshot->solution_source != extra->expected_source) {
        fprintf(stderr, "expected final source %s, got %s\n", nav_solution_source_to_string(extra->expected_source), nav_solution_source_to_string(snapshot->solution_source));
        log_line(logs, "t=%lu level=ERROR cat=REPLAY event=expected_final_mismatch field=source expected=%s actual=%s",
                 (unsigned long)snapshot->time_ms, nav_solution_source_to_string(extra->expected_source), nav_solution_source_to_string(snapshot->solution_source));
        ok = false;
    }
    if (extra->has_expected_reject && snapshot->reject_reason != extra->expected_reject) {
        fprintf(stderr, "expected final reject %s, got %s\n", nav_reject_reason_to_string(extra->expected_reject), nav_reject_reason_to_string(snapshot->reject_reason));
        log_line(logs, "t=%lu level=ERROR cat=REPLAY event=expected_final_mismatch field=reject expected=%s actual=%s",
                 (unsigned long)snapshot->time_ms, nav_reject_reason_to_string(extra->expected_reject), nav_reject_reason_to_string(snapshot->reject_reason));
        ok = false;
    }
    return ok;
}

static int replay_file(const replay_options_t *options)
{
    if (mkdir_recursive(options->out_dir) != 0) {
        fprintf(stderr, "failed to create output directory: %s\n", options->out_dir);
        return 2;
    }

    char solution_path[REPLAY_PATH_LEN];
    char peers_path[REPLAY_PATH_LEN];
    char logs_path[REPLAY_PATH_LEN];
    char compare_txt_path[REPLAY_PATH_LEN];
    char compare_json_path[REPLAY_PATH_LEN];
    if (!join_path(solution_path, sizeof(solution_path), options->out_dir, "solution.csv") ||
        !join_path(peers_path, sizeof(peers_path), options->out_dir, "peers.csv") ||
        !join_path(logs_path, sizeof(logs_path), options->out_dir, "logs.txt") ||
        !join_path(compare_txt_path, sizeof(compare_txt_path), options->out_dir, "compare_report.txt") ||
        !join_path(compare_json_path, sizeof(compare_json_path), options->out_dir, "compare_report.json")) {
        fprintf(stderr, "output path too long\n");
        return 2;
    }

    char fixture_dir[REPLAY_PATH_LEN];
    char auto_config_path[REPLAY_PATH_LEN];
    char auto_truth_path[REPLAY_PATH_LEN];
    const char *config_path = NULL;
    const char *truth_path = NULL;
    if (!dirname_from_path(fixture_dir, sizeof(fixture_dir), options->events_path)) {
        fprintf(stderr, "events path too long\n");
        return 2;
    }
    if (!options->no_config) {
        if (options->config_path != NULL) {
            config_path = options->config_path;
        } else if (join_path(auto_config_path, sizeof(auto_config_path), fixture_dir, "replay_config.csv") && file_exists(auto_config_path)) {
            config_path = auto_config_path;
        }
    }
    if (!options->no_truth) {
        if (options->truth_path != NULL) {
            truth_path = options->truth_path;
        } else if (join_path(auto_truth_path, sizeof(auto_truth_path), fixture_dir, "truth.csv") && file_exists(auto_truth_path)) {
            truth_path = auto_truth_path;
        }
    }

    FILE *events = fopen(options->events_path, "r");
    if (events == NULL) {
        fprintf(stderr, "failed to open events file: %s\n", options->events_path);
        return 2;
    }
    FILE *solution = fopen(solution_path, "w");
    FILE *peers = fopen(peers_path, "w");
    FILE *logs = fopen(logs_path, "w");
    if (solution == NULL || peers == NULL || logs == NULL) {
        fprintf(stderr, "failed to open replay outputs in %s\n", options->out_dir);
        fclose(events);
        if (solution != NULL) fclose(solution);
        if (peers != NULL) fclose(peers);
        if (logs != NULL) fclose(logs);
        return 2;
    }

    nav_config_t config = nav_config_default(options->node_id);
    config.demo_force_gps_denied = true;
    config.min_anchor_triangle_area_m2 = 10.0f;
    config.degraded_anchor_triangle_area_m2 = 100.0f;
    replay_config_extra_t replay_extra;
    replay_config_extra_init(&replay_extra);
    if (config_path != NULL && !load_replay_config(config_path, &config, &replay_extra)) {
        fclose(events);
        fclose(solution);
        fclose(peers);
        fclose(logs);
        return 1;
    }
    if (options->node_id_override) {
        config.local_node_id = options->node_id;
    }

    replay_truth_table_t truth;
    replay_truth_table_t *truth_ptr = NULL;
    if (truth_path != NULL) {
        if (!load_truth_csv(truth_path, &truth)) {
            fclose(events);
            fclose(solution);
            fclose(peers);
            fclose(logs);
            return 1;
        }
        truth_ptr = &truth;
    }

    replay_context_t ctx = {.logs = logs};
    log_line(logs, "t=0 level=INFO cat=REPLAY event=replay_start events=%s out_dir=%s node_id=%u config=%s truth=%s",
             options->events_path,
             options->out_dir,
             config.local_node_id,
             config_path == NULL ? "NONE" : config_path,
             truth_path == NULL ? "NONE" : truth_path);

    replay_compare_stats_t compare_stats = {0};
    nav_system_t sys;
    nav_core_init(&sys, &config);
    nav_logger_t logger;
    nav_logger_init(&logger, core_log_callback, logs, NAV_LOG_TRACE);
    nav_core_set_logger(&sys, &logger);

    write_solution_header(solution);
    write_peers_header(peers);

    char line[REPLAY_MAX_LINE_LEN];
    unsigned line_no = 0u;
    bool header_seen = false;
    replay_header_t header;
    uint32_t last_time_ms = 0u;
    bool have_time = false;
    int status = 0;

    while (fgets(line, sizeof(line), events) != NULL) {
        ++line_no;
        char original[REPLAY_MAX_LINE_LEN];
        strncpy(original, line, sizeof(original));
        original[sizeof(original) - 1u] = '\0';
        if (line_is_ignored(line)) {
            continue;
        }
        replay_row_t row;
        if (split_csv_line(original, &row) < 0) {
            fprintf(stderr, "%s:%u: malformed CSV line\n", options->events_path, line_no);
            status = 1;
            break;
        }
        if (!header_seen) {
            if (!parse_header(&row, &header)) {
                status = 1;
                break;
            }
            header_seen = true;
            continue;
        }

        replay_event_type_t replay_type;
        nav_event_t event;
        if (!build_event(logs, options->events_path, line_no, &header, &row, &replay_type, &event)) {
            status = 1;
            break;
        }

        if (have_time && event.timestamp_ms < last_time_ms) {
            fprintf(stderr, "%s:%u: time_ms went backwards\n", options->events_path, line_no);
            status = 1;
            break;
        }

        ++ctx.events_read;
        if (have_time && event.timestamp_ms > last_time_ms && replay_type != REPLAY_EVT_TICK) {
            nav_core_tick(&sys, event.timestamp_ms);
        }

        if (replay_type == REPLAY_EVT_TICK) {
            nav_core_tick(&sys, event.timestamp_ms);
        } else {
            nav_core_handle_event(&sys, &event);
        }
        ++ctx.events_applied;
        log_line(logs, "t=%lu level=INFO cat=REPLAY event=event_applied line=%u event_type=%s", (unsigned long)event.timestamp_ms, line_no, field(&header, &row, COL_EVENT_TYPE));
        if (!emit_outputs(solution, peers, &sys, event.timestamp_ms, &ctx, truth_ptr, &compare_stats)) {
            status = 1;
            break;
        }
        last_time_ms = event.timestamp_ms;
        have_time = true;
    }

    if (!header_seen && status == 0) {
        fprintf(stderr, "%s: missing header row\n", options->events_path);
        status = 1;
    }

    nav_snapshot_t final_snapshot;
    (void)nav_core_get_snapshot(&sys, &final_snapshot);
    bool compare_passed = true;
    if (truth_ptr != NULL) {
        if (!write_compare_reports(compare_txt_path, compare_json_path, &compare_stats, &replay_extra)) {
            status = 1;
            compare_passed = false;
        } else {
            compare_passed = compare_stats_pass(&compare_stats, &replay_extra);
            log_line(
                logs,
                "t=%lu level=%s cat=REPLAY event=truth_compare rows_compared=%u rows_skipped_no_truth=%u rows_skipped_no_radio_solution=%u max_horizontal_error_m=%.6f max_vertical_error_m=%.6f max_3d_error_m=%.6f pass=%u",
                (unsigned long)final_snapshot.time_ms,
                compare_passed ? "INFO" : "ERROR",
                compare_stats.rows_compared,
                compare_stats.rows_skipped_no_truth,
                compare_stats.rows_skipped_no_radio_solution,
                compare_stats.max_horizontal_error_m,
                compare_stats.max_vertical_error_m,
                compare_stats.max_3d_error_m,
                compare_passed ? 1u : 0u
            );
            if (!compare_passed) {
                fprintf(stderr, "truth comparison failed; see %s\n", compare_txt_path);
                status = 1;
            }
        }
    } else {
        log_line(logs, "t=%lu level=INFO cat=REPLAY event=truth_compare skipped=1 reason=NO_TRUTH", (unsigned long)final_snapshot.time_ms);
    }
    if (!validate_expected_final(&replay_extra, &final_snapshot, logs)) {
        status = 1;
    }
    log_line(
        logs,
        "t=%lu level=INFO cat=REPLAY event=replay_summary events_read=%u events_applied=%u solutions_written=%u final_mode=%s final_solution=%s final_source=%s final_reject=%s",
        (unsigned long)final_snapshot.time_ms,
        ctx.events_read,
        ctx.events_applied,
        ctx.solutions_written,
        nav_mode_to_string(final_snapshot.nav_mode),
        nav_solution_status_to_string(final_snapshot.solution_status),
        nav_solution_source_to_string(final_snapshot.solution_source),
        nav_reject_reason_to_string(final_snapshot.reject_reason)
    );

    printf("nav_replay summary:\n");
    printf("  events_read=%u\n", ctx.events_read);
    printf("  events_applied=%u\n", ctx.events_applied);
    printf("  solutions_written=%u\n", ctx.solutions_written);
    printf("  final_mode=%s\n", nav_mode_to_string(final_snapshot.nav_mode));
    printf("  final_solution=%s\n", nav_solution_status_to_string(final_snapshot.solution_status));
    printf("  final_source=%s\n", nav_solution_source_to_string(final_snapshot.solution_source));
    printf("  final_reject=%s\n", nav_reject_reason_to_string(final_snapshot.reject_reason));
    if (truth_ptr != NULL) {
        printf("  truth_rows=%lu\n", (unsigned long)truth_ptr->count);
        printf("  rows_compared=%u\n", compare_stats.rows_compared);
        printf("  truth_compare=%s\n", compare_passed ? "PASS" : "FAIL");
    } else {
        printf("  truth_compare=SKIPPED\n");
    }
    if (options->pretty) {
        printf("  solution_csv=%s\n", solution_path);
        printf("  peers_csv=%s\n", peers_path);
        printf("  logs_txt=%s\n", logs_path);
        if (truth_ptr != NULL) {
            printf("  compare_report_txt=%s\n", compare_txt_path);
            printf("  compare_report_json=%s\n", compare_json_path);
        }
    }

    fclose(events);
    fclose(solution);
    fclose(peers);
    fclose(logs);
    return status;
}

int main(int argc, char **argv)
{
    replay_options_t options;
    if (!parse_options(argc, argv, &options)) {
        usage(argv[0]);
        return 2;
    }
    return replay_file(&options);
}
