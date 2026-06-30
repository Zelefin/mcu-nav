#ifndef NAV_SERIAL_JSON_H
#define NAV_SERIAL_JSON_H

#include "nav/nav_peer_table.h"
#include "nav/nav_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Control channel codec: the newline-delimited JSON spoken over a node's
 * USB-serial port to the browser control-app. This module is the single source
 * of truth for that wire format and is fully host-testable; the port layer only
 * supplies bytes and applies parsed commands. */

#define NAV_NODE_NAME_MAX 24u

/* Per-node header echoed in every snapshot line. */
typedef struct {
    uint8_t node_id;
    const char *node_name; /* may be NULL */
    bool gps_enabled;
    bool mock_enabled;
} nav_serial_node_info_t;

/* Commands the control-app can send. Flat, one verb per line. */
typedef enum {
    NAV_CTRL_CMD_NONE = 0,
    NAV_CTRL_CMD_GET,          /* request an immediate snapshot */
    NAV_CTRL_CMD_SET_GPS,      /* bool_value: enable local GNSS vs force denied */
    NAV_CTRL_CMD_SET_MOCK,     /* bool_value: enable mock peer source */
    NAV_CTRL_CMD_SET_NAME,     /* str_value: new node name */
    NAV_CTRL_CMD_SET_ALTITUDE, /* int_value: constant altitude in mm */
    NAV_CTRL_CMD_UNKNOWN
} nav_ctrl_cmd_type_t;

typedef struct {
    nav_ctrl_cmd_type_t type;
    bool bool_value;
    int32_t int_value;
    char str_value[NAV_NODE_NAME_MAX];
} nav_ctrl_command_t;

/* Serialises the snapshot and peer table as one JSON object (no trailing
 * newline). Returns the number of bytes that would be written (snprintf
 * semantics), or a negative value on a null argument. */
int nav_serial_write_snapshot(
    char *buf,
    size_t cap,
    const nav_serial_node_info_t *info,
    const nav_snapshot_t *snapshot,
    const nav_peer_table_t *peers
);

/* Parses one command line. Tolerant of surrounding whitespace and key order.
 * Returns NAV_STATUS_OK with out->type set (possibly NAV_CTRL_CMD_UNKNOWN for a
 * well-formed object with an unknown verb), or NAV_STATUS_BAD_FRAME if the line
 * is not a usable JSON object. */
nav_status_t nav_serial_parse_command(const char *line, nav_ctrl_command_t *out);

#ifdef __cplusplus
}
#endif

#endif
