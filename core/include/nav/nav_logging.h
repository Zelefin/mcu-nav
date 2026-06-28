#ifndef NAV_LOGGING_H
#define NAV_LOGGING_H

#include "nav/nav_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NAV_LOG_TRACE = 0,
    NAV_LOG_DEBUG,
    NAV_LOG_INFO,
    NAV_LOG_WARN,
    NAV_LOG_ERROR
} nav_log_level_t;

typedef enum {
    NAV_LOG_CAT_BOOT = 0,
    NAV_LOG_CAT_CONFIG,
    NAV_LOG_CAT_GNSS,
    NAV_LOG_CAT_RADIO_PROTO,
    NAV_LOG_CAT_PEER_TABLE,
    NAV_LOG_CAT_RANGE,
    NAV_LOG_CAT_QUALITY,
    NAV_LOG_CAT_STATE,
    NAV_LOG_CAT_SOLUTION,
    NAV_LOG_CAT_REPLAY,
    NAV_LOG_CAT_SIM,
    NAV_LOG_CAT_ERROR
} nav_log_category_t;

typedef void (*nav_log_callback_t)(
    uint32_t timestamp_ms,
    nav_log_level_t level,
    nav_log_category_t category,
    const char *event,
    const char *message,
    void *user
);

typedef struct {
    nav_log_callback_t callback;
    void *user;
    nav_log_level_t min_level;
} nav_logger_t;

void nav_logger_init(nav_logger_t *logger, nav_log_callback_t callback, void *user, nav_log_level_t min_level);
void nav_log_emit(
    const nav_logger_t *logger,
    uint32_t timestamp_ms,
    nav_log_level_t level,
    nav_log_category_t category,
    const char *event,
    const char *message
);
const char *nav_log_level_to_string(nav_log_level_t level);
const char *nav_log_category_to_string(nav_log_category_t category);

#ifdef __cplusplus
}
#endif

#endif
