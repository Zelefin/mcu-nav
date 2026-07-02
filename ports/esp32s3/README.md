# ESP32-S3 Port Notes

The repository-root ESP-IDF firmware currently owns ESP32/ESP32-S3 hardware
bring-up, board pin selection, health checks, persisted config, and the
USB-serial control channel. The portable core must remain independent from
ESP-IDF, FreeRTOS, UART drivers, RadioLib, and board-specific headers.

For wiring and module bring-up, use the repository-root PlatformIO diagnostic
firmware. It checks the radio, GPS, and compass modules without integrating them
fully into the navigation core.

Expected ESP32 integration responsibilities:

- Adapt GNSS driver samples into `nav_event_t`.
- Run the TDMA scheduler from platform code.
- Use normal SX1280 packet mode for telemetry slots.
- Use the SX1280 ranging engine for ranging slots. The reference workflow is the
  self-contained `examples/esp32s3-ranging` firmware using RadioLib
  `startRanging()` / `finishRanging()`.
- Convert successful ranging-engine measurements into `NAV_EVT_RANGE_RESULT`
  and failures into `NAV_EVT_RANGE_FAIL`.
- Carry both endpoints for range results/failures: `from_id` is the scheduled
  ranging master and `to_id` is the scheduled ranging slave. Third-party pair
  ranges are network-health/control-app data unless one endpoint is the local
  node.
- Forward structured logs and CSV streams to the selected debug sink.
- Provide a timer source and persistent configuration storage.

Distance must not be estimated from RSSI, SNR, host-side packet round trips, or
packet timestamps. Those values are diagnostics and scheduling inputs only.
