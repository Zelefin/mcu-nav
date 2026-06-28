#ifndef NAV_CORE_H
#define NAV_CORE_H

#include "nav/nav_config.h"
#include "nav/nav_events.h"
#include "nav/nav_logging.h"
#include "nav/nav_peer_table.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    nav_config_t config;
    nav_peer_table_t peer_table;
    nav_logger_t logger;
    nav_gnss_sample_t local_gnss;
    nav_local_altitude_t local_altitude;
    bool local_gnss_present;
    bool local_altitude_present;
    nav_mode_t mode;
    nav_snapshot_t snapshot;
    uint32_t last_tick_ms;
} nav_system_t;

void nav_core_init(nav_system_t *sys, const nav_config_t *config);
void nav_core_set_logger(nav_system_t *sys, const nav_logger_t *logger);
void nav_core_handle_event(nav_system_t *sys, const nav_event_t *event);
void nav_core_tick(nav_system_t *sys, uint32_t now_ms);
bool nav_core_get_snapshot(const nav_system_t *sys, nav_snapshot_t *out);
nav_status_t nav_select_radio_anchors(const nav_system_t *sys, uint32_t now_ms, nav_anchor_selection_t *out);

#ifdef __cplusplus
}
#endif

#endif
