#ifndef NAV_NMEA_H
#define NAV_NMEA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "nav/nav_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NAV_NMEA_MAX_SENTENCE_LEN 96u

typedef enum {
    NAV_NMEA_RESULT_NONE = 0,
    NAV_NMEA_RESULT_SAMPLE,
    NAV_NMEA_RESULT_CHECKSUM_ERROR,
    NAV_NMEA_RESULT_UNSUPPORTED_SENTENCE,
    NAV_NMEA_RESULT_MALFORMED_SENTENCE,
    NAV_NMEA_RESULT_OVERFLOW
} nav_nmea_result_t;

typedef struct {
    char sentence[NAV_NMEA_MAX_SENTENCE_LEN];
    size_t length;
    bool in_sentence;
} nav_nmea_parser_t;

void nav_nmea_parser_init(nav_nmea_parser_t *parser);
nav_nmea_result_t nav_nmea_parser_push(nav_nmea_parser_t *parser, uint8_t byte, nav_gnss_sample_t *out_sample);
void nav_gnss_sample_set_time(nav_gnss_sample_t *sample, uint32_t now_ms);
const char *nav_nmea_result_to_string(nav_nmea_result_t result);

#ifdef __cplusplus
}
#endif

#endif
