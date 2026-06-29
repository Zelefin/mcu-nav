# GNSS NMEA Tools

`nav_nmea_dump` is a host utility for inspecting NMEA text logs with the same
portable parser used by the core adapter path.

Build:

```bash
cmake -S . -B build
cmake --build build
```

Run:

```bash
./build/tools/gnss/nav_nmea_dump examples/gnss/valid_gga_rmc.nmea
./build/tools/gnss/nav_nmea_dump examples/gnss/mixed_noise.nmea
```

The tool feeds the file byte by byte into `nav_nmea_parser_push()` and prints
one line per emitted `nav_gnss_sample_t`. It returns nonzero for checksum,
malformed, or overflow errors unless `--tolerate-errors` is passed.

Supported parser inputs are `$GPGGA`, `$GNGGA`, `$GPRMC`, and `$GNRMC`.
Unsupported valid-checksum sentences are reported but are not fatal.

The parser emits `timestamp_ms=0`; UART/replay adapters are responsible for
stamping samples with local system time before creating
`NAV_EVT_LOCAL_GNSS_SAMPLE`.

This is a POSIX-style host diagnostic tool. The parser itself lives in
`core/src/nav_nmea.c`, uses fixed buffers, and does not use file I/O, malloc, or
platform APIs.
