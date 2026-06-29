#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "nav/nav_nmea.h"

static void print_usage(const char *argv0)
{
    fprintf(stderr, "usage: %s [--tolerate-errors] <nmea-file>\n", argv0);
}

static void print_sample(const nav_gnss_sample_t *sample)
{
    printf(
        "sample valid=%u fix_type=%d lat_e7=%ld lon_e7=%ld alt_mm=%ld satellites=%u hdop_centi=%u "
        "hacc_mm=%lu vacc_mm=%lu timestamp_ms=%lu\n",
        sample->valid ? 1u : 0u,
        (int)sample->fix_type,
        (long)sample->position.lat_e7,
        (long)sample->position.lon_e7,
        (long)sample->position.alt_mm,
        sample->satellites,
        sample->hdop_centi,
        (unsigned long)sample->hacc_mm,
        (unsigned long)sample->vacc_mm,
        (unsigned long)sample->timestamp_ms
    );
}

int main(int argc, char **argv)
{
    bool tolerate_errors = false;
    const char *path = NULL;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--tolerate-errors") == 0) {
            tolerate_errors = true;
        } else if (path == NULL) {
            path = argv[i];
        } else {
            print_usage(argv[0]);
            return 2;
        }
    }

    if (path == NULL) {
        print_usage(argv[0]);
        return 2;
    }

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        perror(path);
        return 1;
    }

    nav_nmea_parser_t parser;
    nav_nmea_parser_init(&parser);
    nav_gnss_sample_t sample = {0};
    bool had_error = false;
    unsigned long byte_offset = 0u;

    int ch = 0;
    while ((ch = fgetc(file)) != EOF) {
        ++byte_offset;
        const nav_nmea_result_t result = nav_nmea_parser_push(&parser, (uint8_t)ch, &sample);
        if (result == NAV_NMEA_RESULT_SAMPLE) {
            print_sample(&sample);
        } else if (result == NAV_NMEA_RESULT_UNSUPPORTED_SENTENCE) {
            fprintf(stderr, "%s at byte %lu\n", nav_nmea_result_to_string(result), byte_offset);
        } else if (
            result == NAV_NMEA_RESULT_CHECKSUM_ERROR || result == NAV_NMEA_RESULT_MALFORMED_SENTENCE ||
            result == NAV_NMEA_RESULT_OVERFLOW) {
            fprintf(stderr, "%s at byte %lu\n", nav_nmea_result_to_string(result), byte_offset);
            had_error = true;
        }
    }

    if (ferror(file)) {
        perror(path);
        (void)fclose(file);
        return 1;
    }
    (void)fclose(file);

    if (had_error && !tolerate_errors) {
        return 1;
    }
    return 0;
}
