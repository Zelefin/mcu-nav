#ifndef NAV_EVENTS_H
#define NAV_EVENTS_H

#include "nav/nav_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NAV_EVT_NONE = 0,
    NAV_EVT_TICK,
    NAV_EVT_LOCAL_GNSS_SAMPLE,
    NAV_EVT_LOCAL_ALTITUDE_SAMPLE,
    NAV_EVT_PEER_TELEMETRY_RX,
    NAV_EVT_RANGE_RESULT,
    NAV_EVT_RANGE_FAIL,
    NAV_EVT_CONFIG_COMMAND
} nav_event_type_t;

typedef enum {
    NAV_CONFIG_CMD_NONE = 0,
    NAV_CONFIG_CMD_FORCE_GPS_DENIED,
    NAV_CONFIG_CMD_USE_GNSS
} nav_config_command_type_t;

typedef struct {
    nav_config_command_type_t command;
    bool enabled;
} nav_config_command_t;

typedef struct {
    nav_event_type_t type;
    uint32_t timestamp_ms;
    union {
        nav_gnss_sample_t local_gnss;
        nav_local_altitude_t local_altitude;
        nav_peer_beacon_rx_t peer_beacon_rx;
        nav_range_result_t range_result;
        nav_range_failure_t range_failure;
        nav_config_command_t config_command;
    } data;
} nav_event_t;

#ifdef __cplusplus
}
#endif

#endif
