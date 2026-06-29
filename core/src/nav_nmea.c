#include "nav/nav_nmea.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define NAV_NMEA_MAX_FIELDS 24u

static void sample_clear(nav_gnss_sample_t *sample)
{
    if (sample != NULL) {
        memset(sample, 0, sizeof(*sample));
        sample->fix_type = NAV_GNSS_FIX_NONE;
    }
}

void nav_nmea_parser_init(nav_nmea_parser_t *parser)
{
    if (parser == NULL) {
        return;
    }
    memset(parser, 0, sizeof(*parser));
}

void nav_gnss_sample_set_time(nav_gnss_sample_t *sample, uint32_t now_ms)
{
    if (sample != NULL) {
        sample->timestamp_ms = now_ms;
    }
}

const char *nav_nmea_result_to_string(nav_nmea_result_t result)
{
    switch (result) {
    case NAV_NMEA_RESULT_NONE:
        return "NONE";
    case NAV_NMEA_RESULT_SAMPLE:
        return "SAMPLE";
    case NAV_NMEA_RESULT_CHECKSUM_ERROR:
        return "CHECKSUM_ERROR";
    case NAV_NMEA_RESULT_UNSUPPORTED_SENTENCE:
        return "UNSUPPORTED_SENTENCE";
    case NAV_NMEA_RESULT_MALFORMED_SENTENCE:
        return "MALFORMED_SENTENCE";
    case NAV_NMEA_RESULT_OVERFLOW:
        return "OVERFLOW";
    default:
        return "UNKNOWN";
    }
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return -1;
}

static bool parse_long_field(const char *field, long min_value, long max_value, long *out)
{
    if (field == NULL || field[0] == '\0' || out == NULL) {
        return false;
    }
    errno = 0;
    char *end = NULL;
    const long value = strtol(field, &end, 10);
    if (errno != 0 || end == field || *end != '\0' || value < min_value || value > max_value) {
        return false;
    }
    *out = value;
    return true;
}

static bool parse_double_field(const char *field, double *out)
{
    if (field == NULL || field[0] == '\0' || out == NULL) {
        return false;
    }
    errno = 0;
    char *end = NULL;
    const double value = strtod(field, &end);
    if (errno != 0 || end == field || *end != '\0' || !isfinite(value)) {
        return false;
    }
    *out = value;
    return true;
}

static bool round_to_i32(double value, int32_t *out)
{
    if (out == NULL || !isfinite(value)) {
        return false;
    }
    const double rounded = round(value);
    if (rounded < (double)INT32_MIN || rounded > (double)INT32_MAX) {
        return false;
    }
    *out = (int32_t)rounded;
    return true;
}

static bool round_to_u16(double value, uint16_t *out)
{
    if (out == NULL || !isfinite(value)) {
        return false;
    }
    const double rounded = round(value);
    if (rounded < 0.0 || rounded > (double)UINT16_MAX) {
        return false;
    }
    *out = (uint16_t)rounded;
    return true;
}

static bool parse_coordinate_e7(const char *value_field, const char *hemisphere_field, bool longitude, int32_t *out)
{
    if (value_field == NULL || hemisphere_field == NULL || hemisphere_field[0] == '\0' || hemisphere_field[1] != '\0') {
        return false;
    }

    double raw = 0.0;
    if (!parse_double_field(value_field, &raw) || raw < 0.0) {
        return false;
    }

    const int degrees = (int)(raw / 100.0);
    const double minutes = raw - ((double)degrees * 100.0);
    if (minutes < 0.0 || minutes >= 60.0) {
        return false;
    }

    const char hemisphere = (char)toupper((unsigned char)hemisphere_field[0]);
    if (longitude) {
        if (hemisphere != 'E' && hemisphere != 'W') {
            return false;
        }
        if (degrees > 180) {
            return false;
        }
    } else {
        if (hemisphere != 'N' && hemisphere != 'S') {
            return false;
        }
        if (degrees > 90) {
            return false;
        }
    }

    double decimal_degrees = (double)degrees + (minutes / 60.0);
    if (hemisphere == 'S' || hemisphere == 'W') {
        decimal_degrees = -decimal_degrees;
    }
    return round_to_i32(decimal_degrees * 10000000.0, out);
}

static bool parse_optional_coordinate_e7(const char *value_field, const char *hemisphere_field, bool longitude, int32_t *out)
{
    if (value_field == NULL || value_field[0] == '\0') {
        *out = 0;
        return true;
    }
    return parse_coordinate_e7(value_field, hemisphere_field, longitude, out);
}

static nav_gnss_fix_type_t fix_type_from_gga_quality(long quality)
{
    switch (quality) {
    case 0:
        return NAV_GNSS_FIX_NONE;
    case 4:
        return NAV_GNSS_FIX_RTK_FIXED;
    case 5:
        return NAV_GNSS_FIX_RTK_FLOAT;
    case 1:
    case 2:
    default:
        return NAV_GNSS_FIX_3D;
    }
}

static size_t split_fields(char *payload, char *fields[NAV_NMEA_MAX_FIELDS])
{
    size_t count = 0u;
    fields[count++] = payload;
    for (char *cursor = payload; *cursor != '\0' && count < NAV_NMEA_MAX_FIELDS; ++cursor) {
        if (*cursor == ',') {
            *cursor = '\0';
            fields[count++] = cursor + 1;
        }
    }
    return count;
}

static nav_nmea_result_t parse_gga(char *fields[NAV_NMEA_MAX_FIELDS], size_t field_count, nav_gnss_sample_t *out_sample)
{
    if (field_count < 10u) {
        return NAV_NMEA_RESULT_MALFORMED_SENTENCE;
    }

    long fix_quality = 0;
    long satellites = 0;
    double hdop = 0.0;
    double altitude_m = 0.0;
    if (!parse_long_field(fields[6], 0, 99, &fix_quality)) {
        return NAV_NMEA_RESULT_MALFORMED_SENTENCE;
    }
    if (!parse_long_field(fields[7], 0, UINT8_MAX, &satellites)) {
        return NAV_NMEA_RESULT_MALFORMED_SENTENCE;
    }
    if (fields[8][0] != '\0' && !parse_double_field(fields[8], &hdop)) {
        return NAV_NMEA_RESULT_MALFORMED_SENTENCE;
    }
    if (fields[9][0] != '\0' && !parse_double_field(fields[9], &altitude_m)) {
        return NAV_NMEA_RESULT_MALFORMED_SENTENCE;
    }

    nav_gnss_sample_t sample = {0};
    sample.fix_type = fix_type_from_gga_quality(fix_quality);
    sample.valid = fix_quality != 0;
    sample.satellites = (uint8_t)satellites;
    sample.hacc_mm = 0u;
    sample.vacc_mm = 0u;

    if (fields[8][0] != '\0' && !round_to_u16(hdop * 100.0, &sample.hdop_centi)) {
        return NAV_NMEA_RESULT_MALFORMED_SENTENCE;
    }
    if (sample.valid) {
        if (!parse_coordinate_e7(fields[2], fields[3], false, &sample.position.lat_e7) ||
            !parse_coordinate_e7(fields[4], fields[5], true, &sample.position.lon_e7)) {
            return NAV_NMEA_RESULT_MALFORMED_SENTENCE;
        }
    } else {
        if (!parse_optional_coordinate_e7(fields[2], fields[3], false, &sample.position.lat_e7) ||
            !parse_optional_coordinate_e7(fields[4], fields[5], true, &sample.position.lon_e7)) {
            return NAV_NMEA_RESULT_MALFORMED_SENTENCE;
        }
    }
    if (fields[9][0] != '\0' && !round_to_i32(altitude_m * 1000.0, &sample.position.alt_mm)) {
        return NAV_NMEA_RESULT_MALFORMED_SENTENCE;
    }

    *out_sample = sample;
    return NAV_NMEA_RESULT_SAMPLE;
}

static nav_nmea_result_t parse_rmc(char *fields[NAV_NMEA_MAX_FIELDS], size_t field_count, nav_gnss_sample_t *out_sample)
{
    if (field_count < 7u || fields[2][0] == '\0' || fields[2][1] != '\0') {
        return NAV_NMEA_RESULT_MALFORMED_SENTENCE;
    }

    const char status = (char)toupper((unsigned char)fields[2][0]);
    if (status != 'A' && status != 'V') {
        return NAV_NMEA_RESULT_MALFORMED_SENTENCE;
    }

    nav_gnss_sample_t sample = {0};
    sample.valid = status == 'A';
    sample.fix_type = sample.valid ? NAV_GNSS_FIX_3D : NAV_GNSS_FIX_NONE;
    sample.hacc_mm = 0u;
    sample.vacc_mm = 0u;

    if (sample.valid) {
        if (!parse_coordinate_e7(fields[3], fields[4], false, &sample.position.lat_e7) ||
            !parse_coordinate_e7(fields[5], fields[6], true, &sample.position.lon_e7)) {
            return NAV_NMEA_RESULT_MALFORMED_SENTENCE;
        }
    } else {
        if (!parse_optional_coordinate_e7(fields[3], fields[4], false, &sample.position.lat_e7) ||
            !parse_optional_coordinate_e7(fields[5], fields[6], true, &sample.position.lon_e7)) {
            return NAV_NMEA_RESULT_MALFORMED_SENTENCE;
        }
    }

    *out_sample = sample;
    return NAV_NMEA_RESULT_SAMPLE;
}

static nav_nmea_result_t parse_payload(const char *payload, size_t payload_len, nav_gnss_sample_t *out_sample)
{
    if (payload_len == 0u || payload_len >= NAV_NMEA_MAX_SENTENCE_LEN) {
        return NAV_NMEA_RESULT_MALFORMED_SENTENCE;
    }

    char work[NAV_NMEA_MAX_SENTENCE_LEN];
    memcpy(work, payload, payload_len);
    work[payload_len] = '\0';

    char *fields[NAV_NMEA_MAX_FIELDS] = {0};
    const size_t field_count = split_fields(work, fields);
    if (field_count == 0u || strlen(fields[0]) != 5u) {
        return NAV_NMEA_RESULT_MALFORMED_SENTENCE;
    }

    if (strcmp(fields[0], "GPGGA") == 0 || strcmp(fields[0], "GNGGA") == 0) {
        return parse_gga(fields, field_count, out_sample);
    }
    if (strcmp(fields[0], "GPRMC") == 0 || strcmp(fields[0], "GNRMC") == 0) {
        return parse_rmc(fields, field_count, out_sample);
    }
    return NAV_NMEA_RESULT_UNSUPPORTED_SENTENCE;
}

static nav_nmea_result_t parse_sentence(nav_nmea_parser_t *parser, nav_gnss_sample_t *out_sample)
{
    parser->sentence[parser->length] = '\0';
    sample_clear(out_sample);

    char *star = strchr(parser->sentence, '*');
    if (star == NULL || star[1] == '\0' || star[2] == '\0' || star[3] != '\0') {
        return NAV_NMEA_RESULT_MALFORMED_SENTENCE;
    }

    const int high = hex_value(star[1]);
    const int low = hex_value(star[2]);
    if (high < 0 || low < 0) {
        return NAV_NMEA_RESULT_MALFORMED_SENTENCE;
    }

    uint8_t checksum = 0u;
    for (const char *cursor = parser->sentence; cursor < star; ++cursor) {
        checksum ^= (uint8_t)*cursor;
    }
    const uint8_t expected = (uint8_t)((high << 4) | low);
    if (checksum != expected) {
        return NAV_NMEA_RESULT_CHECKSUM_ERROR;
    }

    const size_t payload_len = (size_t)(star - parser->sentence);
    return parse_payload(parser->sentence, payload_len, out_sample);
}

nav_nmea_result_t nav_nmea_parser_push(nav_nmea_parser_t *parser, uint8_t byte, nav_gnss_sample_t *out_sample)
{
    if (parser == NULL || out_sample == NULL) {
        return NAV_NMEA_RESULT_MALFORMED_SENTENCE;
    }

    if (byte == '$') {
        parser->length = 0u;
        parser->in_sentence = true;
        sample_clear(out_sample);
        return NAV_NMEA_RESULT_NONE;
    }

    if (!parser->in_sentence) {
        sample_clear(out_sample);
        return NAV_NMEA_RESULT_NONE;
    }

    if (byte == '\r' || byte == '\n') {
        parser->in_sentence = false;
        if (parser->length == 0u) {
            sample_clear(out_sample);
            return NAV_NMEA_RESULT_NONE;
        }
        return parse_sentence(parser, out_sample);
    }

    if (parser->length + 1u >= NAV_NMEA_MAX_SENTENCE_LEN) {
        parser->length = 0u;
        parser->in_sentence = false;
        sample_clear(out_sample);
        return NAV_NMEA_RESULT_OVERFLOW;
    }

    parser->sentence[parser->length++] = (char)byte;
    sample_clear(out_sample);
    return NAV_NMEA_RESULT_NONE;
}
