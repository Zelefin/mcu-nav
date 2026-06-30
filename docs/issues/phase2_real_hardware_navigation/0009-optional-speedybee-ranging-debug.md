# Optional SpeedyBee Ranging Debug

## What to build

Keep SpeedyBee as an optional SX1280-only ranging debug target. It may help
diagnose ranging behavior, but it must not block the ESP32 Working PoC or the
main radio navigation backlog.

## Acceptance criteria

- [ ] The SpeedyBee target still builds on the Arduino/ESP8266 framework.
- [ ] Any SpeedyBee ranging work reuses the portable core boundary where
      applicable and keeps platform-specific code outside the core.
- [ ] The target is documented as SX1280-only with no GPS.
- [ ] SpeedyBee evidence is useful for ranging diagnostics but is not required
      for ESP32 acceptance.
- [ ] Changes do not regress the ESP32 board paths.

## Blocked by

- None, but do not work on this if it consumes effort needed for the ESP32
  Working PoC or main post-PoC backlog.
