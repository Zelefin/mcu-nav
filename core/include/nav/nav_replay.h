#ifndef NAV_REPLAY_H
#define NAV_REPLAY_H

#include "nav/nav_events.h"

#ifdef __cplusplus
extern "C" {
#endif

nav_status_t nav_replay_parse_event_csv_line(const char *line, nav_event_t *out);

#ifdef __cplusplus
}
#endif

#endif
