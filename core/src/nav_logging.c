#include "nav/nav_logging.h"

#include <stddef.h>

void nav_logger_init(nav_logger_t *logger, nav_log_callback_t callback, void *user, nav_log_level_t min_level)
{
    if (logger == NULL) {
        return;
    }
    logger->callback = callback;
    logger->user = user;
    logger->min_level = min_level;
}

void nav_log_emit(
    const nav_logger_t *logger,
    uint32_t timestamp_ms,
    nav_log_level_t level,
    nav_log_category_t category,
    const char *event,
    const char *message
)
{
    if (logger == NULL || logger->callback == NULL || level < logger->min_level) {
        return;
    }
    logger->callback(timestamp_ms, level, category, event, message, logger->user);
}

const char *nav_log_level_to_string(nav_log_level_t level)
{
    switch (level) {
    case NAV_LOG_TRACE:
        return "TRACE";
    case NAV_LOG_DEBUG:
        return "DEBUG";
    case NAV_LOG_INFO:
        return "INFO";
    case NAV_LOG_WARN:
        return "WARN";
    case NAV_LOG_ERROR:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}

const char *nav_log_category_to_string(nav_log_category_t category)
{
    switch (category) {
    case NAV_LOG_CAT_BOOT:
        return "BOOT";
    case NAV_LOG_CAT_CONFIG:
        return "CONFIG";
    case NAV_LOG_CAT_GNSS:
        return "GNSS";
    case NAV_LOG_CAT_RADIO_PROTO:
        return "RADIO_PROTO";
    case NAV_LOG_CAT_PEER_TABLE:
        return "PEER_TABLE";
    case NAV_LOG_CAT_RANGE:
        return "RANGE";
    case NAV_LOG_CAT_QUALITY:
        return "QUALITY";
    case NAV_LOG_CAT_STATE:
        return "STATE";
    case NAV_LOG_CAT_SOLUTION:
        return "SOLUTION";
    case NAV_LOG_CAT_REPLAY:
        return "REPLAY";
    case NAV_LOG_CAT_SIM:
        return "SIM";
    case NAV_LOG_CAT_ERROR:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}
