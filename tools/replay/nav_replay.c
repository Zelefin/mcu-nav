#include <ctype.h>
#include <errno.h>
#include <limits.h>
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
    uint8_t node_id;
    bool pretty;
} replay_options_t;

typedef struct {
    FILE *logs;
    unsigned events_read;
    unsigned events_applied;
    unsigned solutions_written;
} replay_context_t;

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
    fprintf(stderr, "usage: %s --events events.csv --out-dir output_dir [--node-id N] [--pretty]\n", argv0);
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
        } else if (strcmp(argv[i], "--node-id") == 0 && i + 1 < argc) {
            char *end = NULL;
            long value = strtol(argv[++i], &end, 10);
            if (*end != '\0' || value < 0 || value >= NAV_MAX_NODES) {
                fprintf(stderr, "invalid --node-id: %s\n", argv[i]);
                return false;
            }
            options->node_id = (uint8_t)value;
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

static bool emit_outputs(FILE *solution, FILE *peers, const nav_system_t *sys, uint32_t now_ms, replay_context_t *ctx)
{
    nav_snapshot_t snapshot;
    if (!nav_core_get_snapshot(sys, &snapshot)) {
        return false;
    }
    write_solution_row(solution, &snapshot);
    write_peers_rows(peers, sys, now_ms);
    ++ctx->solutions_written;
    return true;
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
    if (!join_path(solution_path, sizeof(solution_path), options->out_dir, "solution.csv") ||
        !join_path(peers_path, sizeof(peers_path), options->out_dir, "peers.csv") ||
        !join_path(logs_path, sizeof(logs_path), options->out_dir, "logs.txt")) {
        fprintf(stderr, "output path too long\n");
        return 2;
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

    replay_context_t ctx = {.logs = logs};
    log_line(logs, "t=0 level=INFO cat=REPLAY event=replay_start events=%s out_dir=%s node_id=%u", options->events_path, options->out_dir, options->node_id);

    nav_config_t config = nav_config_default(options->node_id);
    config.demo_force_gps_denied = true;
    config.min_anchor_triangle_area_m2 = 10.0f;
    config.degraded_anchor_triangle_area_m2 = 100.0f;
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
        if (!emit_outputs(solution, peers, &sys, event.timestamp_ms, &ctx)) {
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
    if (options->pretty) {
        printf("  solution_csv=%s\n", solution_path);
        printf("  peers_csv=%s\n", peers_path);
        printf("  logs_txt=%s\n", logs_path);
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
