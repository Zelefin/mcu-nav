#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nav/nav_core.h"
#include "nav/nav_nmea.h"

#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            fprintf(stderr, "[nmea_parser] CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #condition);             \
            abort();                                                                                                   \
        }                                                                                                              \
    } while (0)

#define RUN_TEST(fn)                                                                                                   \
    do {                                                                                                               \
        printf("[nmea_parser] %s\n", #fn);                                                                             \
        fn();                                                                                                          \
    } while (0)

static nav_nmea_result_t feed_bytes(nav_nmea_parser_t *parser, const char *text, nav_gnss_sample_t *sample)
{
    nav_nmea_result_t result = NAV_NMEA_RESULT_NONE;
    for (const char *cursor = text; *cursor != '\0'; ++cursor) {
        result = nav_nmea_parser_push(parser, (uint8_t)*cursor, sample);
        if (result != NAV_NMEA_RESULT_NONE) {
            return result;
        }
    }
    return result;
}

static void test_gga_valid_fix(void)
{
    nav_nmea_parser_t parser;
    nav_nmea_parser_init(&parser);
    nav_gnss_sample_t sample = {0};
    const nav_nmea_result_t result =
        feed_bytes(&parser, "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n", &sample);

    CHECK(result == NAV_NMEA_RESULT_SAMPLE);
    CHECK(sample.valid);
    CHECK(sample.fix_type == NAV_GNSS_FIX_3D);
    CHECK(sample.position.lat_e7 == 481173000);
    CHECK(sample.position.lon_e7 == 115166667);
    CHECK(sample.position.alt_mm == 545400);
    CHECK(sample.satellites == 8u);
    CHECK(sample.hdop_centi == 90u);
    CHECK(sample.timestamp_ms == 0u);
}

static void test_gga_invalid_fix(void)
{
    nav_nmea_parser_t parser;
    nav_nmea_parser_init(&parser);
    nav_gnss_sample_t sample = {0};
    const nav_nmea_result_t result =
        feed_bytes(&parser, "$GPGGA,123519,4807.038,N,01131.000,E,0,08,0.9,545.4,M,46.9,M,,*46\n", &sample);

    CHECK(result == NAV_NMEA_RESULT_SAMPLE);
    CHECK(!sample.valid);
    CHECK(sample.fix_type == NAV_GNSS_FIX_NONE);
    CHECK(sample.position.lat_e7 == 481173000);
    CHECK(sample.position.lon_e7 == 115166667);
}

static void test_rmc_valid_status(void)
{
    nav_nmea_parser_t parser;
    nav_nmea_parser_init(&parser);
    nav_gnss_sample_t sample = {0};
    const nav_nmea_result_t result =
        feed_bytes(&parser, "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A\r\n", &sample);

    CHECK(result == NAV_NMEA_RESULT_SAMPLE);
    CHECK(sample.valid);
    CHECK(sample.fix_type == NAV_GNSS_FIX_3D);
    CHECK(sample.position.lat_e7 == 481173000);
    CHECK(sample.position.lon_e7 == 115166667);
    CHECK(sample.position.alt_mm == 0);
    CHECK(sample.velocity.vel_n_mmps == 0);
    CHECK(sample.velocity.vel_e_mmps == 0);
    CHECK(sample.velocity.vel_d_mmps == 0);
}

static void test_rmc_invalid_status(void)
{
    nav_nmea_parser_t parser;
    nav_nmea_parser_init(&parser);
    nav_gnss_sample_t sample = {0};
    const nav_nmea_result_t result =
        feed_bytes(&parser, "$GPRMC,123519,V,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*7D\r\n", &sample);

    CHECK(result == NAV_NMEA_RESULT_SAMPLE);
    CHECK(!sample.valid);
    CHECK(sample.fix_type == NAV_GNSS_FIX_NONE);
}

static void test_checksum_reject(void)
{
    nav_nmea_parser_t parser;
    nav_nmea_parser_init(&parser);
    nav_gnss_sample_t sample = {0};
    const nav_nmea_result_t result =
        feed_bytes(&parser, "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*00\r\n", &sample);

    CHECK(result == NAV_NMEA_RESULT_CHECKSUM_ERROR);
    CHECK(!sample.valid);
}

static void test_malformed_sentence(void)
{
    nav_nmea_parser_t parser;
    nav_nmea_parser_init(&parser);
    nav_gnss_sample_t sample = {0};
    const nav_nmea_result_t result =
        feed_bytes(&parser, "$GPGGA,123519,NOTLAT,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*55\r\n", &sample);

    CHECK(result == NAV_NMEA_RESULT_MALFORMED_SENTENCE);
    CHECK(!sample.valid);
}

static void test_stream_with_noise(void)
{
    nav_nmea_parser_t parser;
    nav_nmea_parser_init(&parser);
    nav_gnss_sample_t sample = {0};
    nav_nmea_result_t result = feed_bytes(
        &parser,
        "noise before\r\n$GNVTG,1,T,2,M,3,N,4,K*54\r\nmore noise$GNGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*59\n",
        &sample
    );

    CHECK(result == NAV_NMEA_RESULT_UNSUPPORTED_SENTENCE);
    result = feed_bytes(
        &parser,
        "more noise$GNGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*59\n",
        &sample
    );
    CHECK(result == NAV_NMEA_RESULT_SAMPLE);
    CHECK(sample.valid);
    CHECK(sample.position.lat_e7 == 481173000);
}

static void test_overlong_sentence(void)
{
    nav_nmea_parser_t parser;
    nav_nmea_parser_init(&parser);
    nav_gnss_sample_t sample = {0};
    nav_nmea_result_t result = nav_nmea_parser_push(&parser, '$', &sample);
    CHECK(result == NAV_NMEA_RESULT_NONE);
    for (size_t i = 0u; i < NAV_NMEA_MAX_SENTENCE_LEN + 8u; ++i) {
        result = nav_nmea_parser_push(&parser, 'A', &sample);
        if (result == NAV_NMEA_RESULT_OVERFLOW) {
            break;
        }
    }
    CHECK(result == NAV_NMEA_RESULT_OVERFLOW);

    result = feed_bytes(&parser, "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\n", &sample);
    CHECK(result == NAV_NMEA_RESULT_SAMPLE);
    CHECK(sample.valid);
}

static void test_gnss_event_into_core(void)
{
    nav_nmea_parser_t parser;
    nav_nmea_parser_init(&parser);
    nav_gnss_sample_t sample = {0};
    CHECK(feed_bytes(&parser, "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\n", &sample) ==
          NAV_NMEA_RESULT_SAMPLE);
    sample.node_id = 0u;
    nav_gnss_sample_set_time(&sample, 1234u);

    nav_system_t sys;
    nav_config_t config = nav_config_default(0u);
    nav_core_init(&sys, &config);
    nav_event_t event = {
        .type = NAV_EVT_LOCAL_GNSS_SAMPLE,
        .timestamp_ms = 1234u,
        .data.local_gnss = sample,
    };
    nav_core_handle_event(&sys, &event);

    nav_snapshot_t snapshot;
    CHECK(nav_core_get_snapshot(&sys, &snapshot));
    CHECK(sys.local_gnss_present);
    CHECK(sys.local_gnss.timestamp_ms == 1234u);
    CHECK(snapshot.solution_status == NAV_SOLUTION_GNSS_DIRECT);
    CHECK(snapshot.solution_source == NAV_SOURCE_LOCAL_GNSS);
    CHECK(snapshot.position.lat_e7 == 481173000);
}

static void test_forced_denied_parsed_gnss_not_solution_source(void)
{
    nav_nmea_parser_t parser;
    nav_nmea_parser_init(&parser);
    nav_gnss_sample_t sample = {0};
    CHECK(feed_bytes(&parser, "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\n", &sample) ==
          NAV_NMEA_RESULT_SAMPLE);
    sample.node_id = 0u;
    nav_gnss_sample_set_time(&sample, 2000u);

    nav_system_t sys;
    nav_config_t config = nav_config_default(0u);
    config.demo_force_gps_denied = true;
    nav_core_init(&sys, &config);
    nav_event_t event = {
        .type = NAV_EVT_LOCAL_GNSS_SAMPLE,
        .timestamp_ms = 2000u,
        .data.local_gnss = sample,
    };
    nav_core_handle_event(&sys, &event);

    nav_snapshot_t snapshot;
    CHECK(nav_core_get_snapshot(&sys, &snapshot));
    CHECK(sys.local_gnss_present);
    CHECK(snapshot.solution_source != NAV_SOURCE_LOCAL_GNSS);
    CHECK(snapshot.solution_status != NAV_SOLUTION_GNSS_DIRECT);
}

int main(void)
{
    CHECK(strcmp(nav_nmea_result_to_string(NAV_NMEA_RESULT_SAMPLE), "SAMPLE") == 0);
    RUN_TEST(test_gga_valid_fix);
    RUN_TEST(test_gga_invalid_fix);
    RUN_TEST(test_rmc_valid_status);
    RUN_TEST(test_rmc_invalid_status);
    RUN_TEST(test_checksum_reject);
    RUN_TEST(test_malformed_sentence);
    RUN_TEST(test_stream_with_noise);
    RUN_TEST(test_overlong_sentence);
    RUN_TEST(test_gnss_event_into_core);
    RUN_TEST(test_forced_denied_parsed_gnss_not_solution_source);
    return 0;
}
