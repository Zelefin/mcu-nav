#include "nav/nav_serial_json.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nav/nav_quality.h"
#include "nav/nav_state_machine.h"

/* ---- snapshot serialisation ---------------------------------------------- */

/* snprintf-style accumulator: appends into buf at the running offset and keeps
 * counting the intended length even after the buffer fills, so the caller learns
 * the size it needs. Passes NULL once full to stay within snprintf's contract. */
static int json_appendf(char *buf, size_t cap, int used, const char *fmt, ...)
{
    if (used < 0) {
        return used;
    }
    const size_t off = (size_t)used;
    char *dst = off < cap ? buf + off : NULL;
    const size_t room = off < cap ? cap - off : 0u;

    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(dst, room, fmt, ap);
    va_end(ap);
    if (n < 0) {
        return -1;
    }
    return used + n;
}

static int json_append_escaped(char *buf, size_t cap, int used, const char *s)
{
    used = json_appendf(buf, cap, used, "\"");
    for (const char *p = s; p != NULL && *p != '\0'; ++p) {
        const unsigned char c = (unsigned char)*p;
        if (c == '"' || c == '\\') {
            used = json_appendf(buf, cap, used, "\\%c", c);
        } else if (c < 0x20u) {
            used = json_appendf(buf, cap, used, "\\u%04x", c);
        } else {
            used = json_appendf(buf, cap, used, "%c", c);
        }
    }
    return json_appendf(buf, cap, used, "\"");
}

static bool solution_is_displayable(nav_solution_source_t source, nav_solution_status_t status)
{
    if (source == NAV_SOURCE_LOCAL_GNSS) {
        return status == NAV_SOLUTION_GNSS_DIRECT;
    }
    if (source == NAV_SOURCE_RADIO_3D) {
        return status == NAV_SOLUTION_RADIO_3D || status == NAV_SOLUTION_DEGRADED;
    }
    return false;
}

static bool solution_is_degraded(nav_solution_source_t source, nav_solution_status_t status)
{
    return source == NAV_SOURCE_RADIO_3D && status == NAV_SOLUTION_DEGRADED;
}

static const char *position_source_to_json(nav_solution_source_t source)
{
    switch (source) {
    case NAV_SOURCE_LOCAL_GNSS:
        return "GNSS";
    case NAV_SOURCE_RADIO_3D:
        return "RADIO_3D";
    default:
        return "NONE";
    }
}

static nav_solution_source_t peer_effective_source(const nav_peer_state_t *peer)
{
    if (peer == NULL) {
        return NAV_SOURCE_NONE;
    }
    if (peer->solution_source != NAV_SOURCE_NONE) {
        return peer->solution_source;
    }
    return peer->gnss_valid ? NAV_SOURCE_LOCAL_GNSS : NAV_SOURCE_NONE;
}

static nav_solution_status_t peer_effective_status(const nav_peer_state_t *peer, nav_solution_source_t source)
{
    if (peer == NULL) {
        return NAV_SOLUTION_NONE;
    }
    if (peer->solution_status != NAV_SOLUTION_NONE) {
        return peer->solution_status;
    }
    return source == NAV_SOURCE_LOCAL_GNSS ? NAV_SOLUTION_GNSS_DIRECT : NAV_SOLUTION_NONE;
}

int nav_serial_write_snapshot(
    char *buf,
    size_t cap,
    const nav_serial_node_info_t *info,
    const nav_snapshot_t *snapshot,
    const nav_peer_table_t *peers
)
{
    if (info == NULL || snapshot == NULL || peers == NULL) {
        return -1;
    }

    int used = 0;
    used = json_appendf(buf, cap, used, "{\"t\":%lu,\"node\":{\"id\":%u,\"name\":",
                        (unsigned long)snapshot->time_ms, (unsigned)info->node_id);
    used = json_append_escaped(buf, cap, used, info->node_name != NULL ? info->node_name : "");
    used = json_appendf(buf, cap, used, ",\"gps\":%s,\"mock\":%s}",
                        info->gps_enabled ? "true" : "false",
                        info->mock_enabled ? "true" : "false");

    used = json_appendf(buf, cap, used,
                        ",\"mode\":\"%s\",\"sol\":\"%s\",\"src\":\"%s\",\"reject\":\"%s\"",
                        nav_mode_to_string(snapshot->nav_mode),
                        nav_solution_status_to_string(snapshot->solution_status),
                        nav_solution_source_to_string(snapshot->solution_source),
                        nav_reject_reason_to_string(snapshot->reject_reason));

    used = json_appendf(buf, cap, used,
                        ",\"pos\":{\"lat_e7\":%ld,\"lon_e7\":%ld,\"alt_mm\":%ld},\"num_anchors\":%u",
                        (long)snapshot->position.lat_e7,
                        (long)snapshot->position.lon_e7,
                        (long)snapshot->position.alt_mm,
                        (unsigned)snapshot->num_anchors);
    used = json_appendf(buf, cap, used,
                        ",\"position_source\":\"%s\",\"position_valid\":%s,\"position_degraded\":%s",
                        position_source_to_json(snapshot->solution_source),
                        solution_is_displayable(snapshot->solution_source, snapshot->solution_status) ? "true"
                                                                                                      : "false",
                        solution_is_degraded(snapshot->solution_source, snapshot->solution_status) ? "true" : "false");

    used = json_appendf(buf, cap, used, ",\"peers\":[");
    bool first = true;
    for (uint8_t i = 0; i < NAV_MAX_NODES; ++i) {
        const nav_peer_state_t *peer = nav_peer_table_get_const(peers, i);
        if (peer == NULL || !peer->present) {
            continue;
        }
        used = json_appendf(buf, cap, used, "%s", first ? "" : ",");
        first = false;
        const nav_solution_source_t source = peer_effective_source(peer);
        const nav_solution_status_t status = peer_effective_status(peer, source);
        const uint32_t telemetry_age_ms = snapshot->time_ms >= peer->last_telemetry_timestamp_ms
                                              ? snapshot->time_ms - peer->last_telemetry_timestamp_ms
                                              : 0u;
        used = json_appendf(buf, cap, used,
                            "{\"id\":%u,\"gnss\":%s,\"lat_e7\":%ld,\"lon_e7\":%ld,\"alt_mm\":%ld,"
                            "\"position_source\":\"%s\",\"position_valid\":%s,\"position_degraded\":%s,"
                            "\"telemetry_age_ms\":%lu,"
                            "\"range_mm\":%lu,\"range_valid\":%s,\"rssi\":%d,\"snr\":%d,\"quality\":%.3f}",
                            (unsigned)peer->node_id,
                            peer->gnss_valid ? "true" : "false",
                            (long)peer->position.lat_e7,
                            (long)peer->position.lon_e7,
                            (long)peer->position.alt_mm,
                            position_source_to_json(source),
                            solution_is_displayable(source, status) ? "true" : "false",
                            solution_is_degraded(source, status) ? "true" : "false",
                            (unsigned long)telemetry_age_ms,
                            (unsigned long)peer->range_mm,
                            peer->range_valid ? "true" : "false",
                            (int)peer->rssi_dbm,
                            (int)peer->snr_db,
                            (double)peer->anchor_quality);
    }
    used = json_appendf(buf, cap, used, "]}");
    return used;
}

/* ---- command parsing ------------------------------------------------------ */

/* These commands are flat, single-level JSON objects, so a tolerant key scanner
 * is enough; we deliberately do not pull in a full JSON parser on the MCU. */

static const char *find_value(const char *line, const char *key)
{
    char needle[32];
    const int n = snprintf(needle, sizeof(needle), "\"%s\"", key);
    if (n < 0 || (size_t)n >= sizeof(needle)) {
        return NULL;
    }
    const char *at = strstr(line, needle);
    if (at == NULL) {
        return NULL;
    }
    at = strchr(at + (size_t)n, ':');
    if (at == NULL) {
        return NULL;
    }
    at++;
    while (*at == ' ' || *at == '\t') {
        at++;
    }
    return at;
}

static bool extract_string(const char *line, const char *key, char *out, size_t cap)
{
    const char *at = find_value(line, key);
    if (at == NULL || *at != '"') {
        return false;
    }
    at++;
    size_t i = 0;
    while (*at != '\0' && *at != '"' && i + 1 < cap) {
        if (*at == '\\' && at[1] != '\0') {
            at++;
        }
        out[i++] = *at++;
    }
    out[i] = '\0';
    return true;
}

static bool extract_bool(const char *line, const char *key, bool *out)
{
    const char *at = find_value(line, key);
    if (at == NULL) {
        return false;
    }
    if (strncmp(at, "true", 4) == 0) {
        *out = true;
        return true;
    }
    if (strncmp(at, "false", 5) == 0) {
        *out = false;
        return true;
    }
    return false;
}

static bool extract_int(const char *line, const char *key, int32_t *out)
{
    const char *at = find_value(line, key);
    if (at == NULL) {
        return false;
    }
    char *end = NULL;
    const long v = strtol(at, &end, 10);
    if (end == at) {
        return false;
    }
    *out = (int32_t)v;
    return true;
}

nav_status_t nav_serial_parse_command(const char *line, nav_ctrl_command_t *out)
{
    if (line == NULL || out == NULL) {
        return NAV_STATUS_INVALID_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));

    char cmd[16];
    if (strchr(line, '{') == NULL || !extract_string(line, "cmd", cmd, sizeof(cmd))) {
        return NAV_STATUS_BAD_FRAME;
    }

    if (strcmp(cmd, "get") == 0) {
        out->type = NAV_CTRL_CMD_GET;
        return NAV_STATUS_OK;
    }
    if (strcmp(cmd, "gps") == 0) {
        out->type = NAV_CTRL_CMD_SET_GPS;
        return extract_bool(line, "enabled", &out->bool_value) ? NAV_STATUS_OK : NAV_STATUS_BAD_FRAME;
    }
    if (strcmp(cmd, "mock") == 0) {
        out->type = NAV_CTRL_CMD_SET_MOCK;
        return extract_bool(line, "enabled", &out->bool_value) ? NAV_STATUS_OK : NAV_STATUS_BAD_FRAME;
    }
    if (strcmp(cmd, "name") == 0) {
        out->type = NAV_CTRL_CMD_SET_NAME;
        return extract_string(line, "value", out->str_value, sizeof(out->str_value)) ? NAV_STATUS_OK
                                                                                     : NAV_STATUS_BAD_FRAME;
    }
    if (strcmp(cmd, "alt") == 0) {
        out->type = NAV_CTRL_CMD_SET_ALTITUDE;
        return extract_int(line, "alt_mm", &out->int_value) ? NAV_STATUS_OK : NAV_STATUS_BAD_FRAME;
    }
    if (strcmp(cmd, "node_id") == 0) {
        out->type = NAV_CTRL_CMD_SET_NODE_ID;
        if (!extract_int(line, "id", &out->int_value)) {
            return NAV_STATUS_BAD_FRAME;
        }
        return out->int_value >= 0 && out->int_value < (int32_t)NAV_MAX_NODES ? NAV_STATUS_OK : NAV_STATUS_BAD_FRAME;
    }

    out->type = NAV_CTRL_CMD_UNKNOWN;
    return NAV_STATUS_OK;
}
