# nav-mcu

Portable navigation-brain firmware for a group UAV navigation system.

This repository owns the main MCU navigation core, ESP navigation-node firmware,
host tests, diagnostics, documentation, and replay/simulation tools. It does
not own separate SX1280 radio firmware, production GNSS hardware adapters, or
flight-controller output.

## ESP Navigation Node Firmware

The repository root is a PlatformIO firmware project that builds the navigation
node for three targets (see Supported Boards). The firmware is a thin port over
the portable `core/`: drivers turn sensor/radio data into `nav_event_t`, the core
runs the peer table and trilateration, and a USB-serial control channel streams a
JSON snapshot to the browser control-app and applies its commands. A boot
self-test checks radio ranging before the node runs. In the current
distance-only firmware slice, GPS and compass runtime health tasks are disabled
by default.

A built-in **mock peer source** lets a single board exercise the full navigation
core and trilateration with no real peers, so most development needs only one
ESP32 + SX1280 + GPS. See "Single-node development" and "Control app" below.

### Boot Self-Test Checks

- E28-2G4M12SX / SX128x radio over SPI with RadioLib.
- Distance-only SX1280 ranging: each node alternates between addressed slave
  listening and active master scans of other node IDs, so a connected node can
  discover its own single-hop range links.
- GPS and compass adapters remain in the tree but are not started in this
  version; health summaries report `GPS=DISABLED` and `COMPASS=DISABLED`.
  Re-enable them with `NAV_ENABLE_GPS_HEALTH=1` and
  `NAV_ENABLE_COMPASS_HEALTH=1` for later GNSS/compass bring-up.
- Periodic FreeRTOS health summary on the serial console.

Radio summary is `WARN` until a ranging exchange succeeds. With only one board
powered, `RADIO=WARN` after successful radio initialization is expected because
no addressed ranging slave can answer.

### Supported Boards

| PlatformIO env | Board | MCU | SDK | Sensors |
| -------------- | ----- | --- | --- | ------- |
| `nodemcu-32s` | NodeMCU-32S | ESP32 | ESP-IDF | SX1280 + GPS + compass |
| `esp32-s3-devkitc-1` | ESP32-S3-DEVKITC-1 | ESP32-S3 | ESP-IDF | SX1280 + GPS + compass |
| `speedybee` | SpeedyBee Nano 2.4G | ESP8285 | Arduino | SX1280 only (no GPS) |

ESP-IDF is not available on the ESP8285, so the SpeedyBee target builds on the
Arduino/ESP8266 framework but reuses the same portable `core/`; only the driver
layer differs. The two ESP32 boards select pins through `build_flags`; SpeedyBee
pins live in `ports/speedybee/include/board_pins.h`. The SpeedyBee board has no
GPS, so it defaults to trilateration — ideal for debugging ranging.

### NodeMCU-32S Wiring

#### E28-2G4M12SX / SX128x

| E28 signal | ESP32 GPIO |
| ---------- | ---------- |
| SCK | 18 |
| MISO | 19 |
| MOSI | 23 |
| NSS / CS | 5 |
| RST | 27 |
| BUSY | 26 |
| DIO1 | 25 |
| DIO2 | NC, unused |
| DIO3 | NC, unused |
| 3V3 | 3.3 V |
| GND | GND |

DIO2 and DIO3 are optional for this basic SX128x health check and are left
unconnected on the NodeMCU-32S wiring.

#### HGLRC M100-5883 GPS

| GPS signal | ESP32 GPIO |
| ---------- | ---------- |
| TX | 16 RX |
| RX | 17 TX |
| VCC | 3.3 V preferred |
| GND | GND |

#### QMC5883 Compass

| Compass signal | ESP32 GPIO |
| -------------- | ---------- |
| SDA | 21 |
| SCL | 22 |
| VCC | 3.3 V preferred |
| GND | GND |

### ESP32-S3-DEVKITC-1 Wiring

#### E28-2G4M12SX / SX128x

| E28 signal | ESP32-S3 GPIO |
| ---------- | ------------- |
| SCK | 12 |
| MISO | 13 |
| MOSI | 11 |
| NSS / CS | 10 |
| RST | 9 |
| BUSY | 8 |
| DIO1 | 7 |
| DIO2 | 6 |
| DIO3 | 15 |
| 3V3 | 3.3 V |
| GND | GND |

#### HGLRC M100-5883 GPS

| GPS signal | ESP32-S3 GPIO |
| ---------- | ------------- |
| TX | 18 RX |
| RX | 17 TX |
| VCC | 3.3 V preferred |
| GND | GND |

#### QMC5883 Compass

| Compass signal | ESP32-S3 GPIO |
| -------------- | ------------- |
| SDA | 4 |
| SCL | 5 |
| VCC | 3.3 V preferred |
| GND | GND |

### PlatformIO Build And Upload

Run commands from the repository root.

Build for NodeMCU-32S:

```bash
pio run -e nodemcu-32s
```

Upload to NodeMCU-32S:

```bash
pio run -e nodemcu-32s -t upload
```

Build for ESP32-S3-DEVKITC-1:

```bash
pio run -e esp32-s3-devkitc-1
```

Upload to ESP32-S3-DEVKITC-1:

```bash
pio run -e esp32-s3-devkitc-1 -t upload
```

Build and upload for SpeedyBee Nano 2.4G (ESP8285):

```bash
pio run -e speedybee
pio run -e speedybee -t upload
```

Monitor serial output:

```bash
pio device monitor -b 115200
```

If `pio` is not on your shell path, use your PlatformIO virtualenv path, for
example:

```bash
~/.platformio/penv/bin/pio run -e nodemcu-32s
```

### Expected Output

```text
t=...ms [INFO] [SYSTEM] Booting firmware
t=...ms [INFO] [SYSTEM] Board: NodeMCU-32S
t=...ms [INFO] [SYSTEM] Build: Jun 30 2026 12:00:00
t=...ms [INFO] [SYSTEM] E28/SX128x pin map:
t=...ms [INFO] [SYSTEM]   SCK=18 MISO=19 MOSI=23 CS=5
t=...ms [INFO] [SYSTEM]   RST=27 BUSY=26 DIO1=25 DIO2=NC DIO3=NC
t=...ms [INFO] [RADIO] Initializing SX128x distance-only ranging
t=...ms [OK] [RADIO] SX128x ranging initialized
t=...ms [INFO] [RANGE] distance-only role node=2 role=single-hop-discovery next_peer=3
t=...ms [OK] [RANGE] range_result ok=true from=2 to=3 request_id=1 range_mm=3420 uncorrected_m=3.42 raw_reg=123 rssi_dbm=-48.5 snr_db=8.0 elapsed_ms=24 irq=0x0010 flags="master_result_valid"
t=...ms [INFO] [RANGE] range_result ok=false from=1 to=2 request_id=7 range_fail_reason=TIMEOUT elapsed_ms=359 error=-901 note="ranging timeout" source=air_report heard_by=3 report_rssi_dbm=-54.0 report_snr_db=13.0
t=...ms [INFO] [GPS] disabled for distance-only firmware
t=...ms [INFO] [COMPASS] disabled for distance-only firmware
t=...ms [INFO] [SYSTEM] Health summary: RADIO=OK GPS=DISABLED COMPASS=DISABLED | radio tx=1 rx=1 gps_bytes=0 gps_sentences=0 compass_xyz=0,0,0
```

### Distance-Only Ranging Procedure

For a full distance-only check, flash this firmware to the four ESP32 boards
with E28/SX128x modules wired.

1. Connect to each board with the control app.
2. Set unique node IDs: one board to `0`, the other boards to `1`, `2`, and
   `3`.
3. Connect the control app to any one node when you want to inspect discovered
   single-hop pair health.
4. Keep the other nodes powered. Every node listens as an addressed slave and
   periodically takes a master ranging turn. After each ranging attempt, the
   master broadcasts a compact best-effort `range_result` report that other
   nodes can show as `source=air_report`.
5. Watch the connected node's serial logs or the control-app network view for
   `range_result ok=true from=<from_id> to=<to_id>` lines and pair distances.

This mode proves SX1280 distance measurements between modules without GNSS. It
does not produce `RADIO_3D`. Third-party pair reports are diagnostics for the
control app only; they are not folded into the local solver anchor table unless
the connected node is one endpoint of the measurement.

The fixed ranging profile is 2445 MHz, SF7, 1625 kHz bandwidth, coding rate
4/5, private SX128x sync word, 12-symbol preamble, and addressed ranging address
`0x4E415600 | node_id`. Keep antennas attached while transmitting.

### Hardware Safety

- E28-2G4M12SX must be powered from 3.3 V only.
- All modules must share common GND.
- HGLRC M100-5883 can often be powered from 3.3 V or 5 V, but 3.3 V is safer
  for ESP32 logic.
- If powering M100-5883 from 5 V, confirm UART TX and I2C SDA/SCL are not
  pulled up to 5 V.
- ESP32 GPIO is not 5 V tolerant.
- QMC5883 is usually at I2C address `0x0D`.
- GPS may not get a fix indoors; NMEA bytes with no fix is a wiring success and
  a sky-view problem.
- ESP32 Wi-Fi/Bluetooth and the E28 2.4 GHz radio share the 2.4 GHz band. This
  firmware does not enable Wi-Fi/Bluetooth.

### Troubleshooting

#### No Radio Detected

- Check 3.3 V power and common GND.
- Check SCK/MISO/MOSI/CS wiring for the selected PlatformIO environment.
- Check RST and BUSY wiring; SX128x init can fail if either is wrong.
- Confirm the module is SX1280/SX1281-compatible.

#### Radio BUSY Stuck HIGH

- Check that BUSY is connected to the configured GPIO.
- Check RST wiring and module power.
- Power-cycle the board and module together.

#### Radio TX Works But RX Never Works

- Use two boards flashed with this same firmware.
- Keep both boards on the same radio build flags.
- Attach antennas.
- Put boards at least tens of centimeters apart to avoid near-field overload.
- Make sure both boards are powered long enough to transmit; default TX interval
  is 10 seconds.

#### No GPS Bytes

This only applies when `NAV_ENABLE_GPS_HEALTH=1`.

- GPS TX must connect to ESP RX.
- Confirm `PIN_GPS_RX` for the active environment.
- Confirm GPS baud is 115200.
- Check GPS power and common GND.

#### GPS Bytes Received But No Fix

This only applies when `NAV_ENABLE_GPS_HEALTH=1`.

- This is normal indoors or near windows with poor sky view.
- Move outdoors and wait for almanac acquisition.
- Check antenna orientation and module backup battery.

#### Compass Not Found

This only applies when `NAV_ENABLE_COMPASS_HEALTH=1`.

- Confirm SDA/SCL are not swapped.
- Check 3.3 V power and common GND.
- Confirm pull-ups are present and not pulled to 5 V.
- Confirm the compass is QMC5883-compatible at address `0x0D`.

#### Compass Found But Raw Data Is Suspicious

This only applies when `NAV_ENABLE_COMPASS_HEALTH=1`.

- Move or rotate the board and watch whether XYZ changes.
- Keep the compass away from USB cables, magnets, motors, and high-current
  wires during the check.

#### Serial Monitor Empty On ESP32-S3

- ESP32-S3 builds use the native USB Serial/JTAG port as the primary ESP-IDF
  console so the control-app can both read snapshots and send JSON commands.
- If stdout works but commands such as `{"cmd":"node_id","id":2}` do not apply,
  remove any stale ignored `sdkconfig.esp32-s3-devkitc-1` file or regenerate it
  from `sdkconfig.defaults.esp32-s3-devkitc-1`.
- Press reset after opening the monitor.
- If one USB port is quiet, try the board's USB serial/JTAG port or UART bridge.

#### Wrong PlatformIO Board ID For ESP32-S3

If your PlatformIO install does not know `esp32-s3-devkitc-1`, list available
boards:

```bash
pio boards espressif32 | grep -i "s3.*devkit"
```

Then replace the `board = esp32-s3-devkitc-1` line in `platformio.ini` with the
board ID from your installed PlatformIO version.

## Current Status

- C11 portable `nav_core` static library builds on Linux.
- Event-in, snapshot/log-out architecture is implemented.
- Peer telemetry and range results update a deterministic peer table.
- The root ESP32 firmware now includes a distance-only ranging PoC: every node
  alternates between addressed slave listening and master scans to record
  single-hop distances without GNSS.
- GPS and compass runtime health tasks are disabled by default for the
  distance-only firmware slice.
- Hardware TDMA range payloads still need to move from implicit local `peer_id`
  semantics to explicit `from_id` / `to_id` endpoints so every node can display
  third-party pair ranges in the control app. Only ranges where one endpoint is
  local should feed the current anchor solver.
- Forced GPS-denied mode can solve a `RADIO_3D` position from three fresh
  GPS-good peer anchors, three ranges, and a valid local altitude sample.
- Snapshot diagnostics include an explicit solution source, so forced-denied
  debug can prove whether a solution came from local GNSS or radio ranging.
- The trilateration adapter is ported from
  `~/projects/trilateration/mcu/esp32_s3_demo/components/trilat`.
- Structured callback logs explain anchor selection, rejections, solve attempts,
  solve success, residuals, and mode/status transitions.
- POSIX demo injects a deterministic forced-denied scenario and prints the
  resulting snapshot.
- Replay CLI consumes deterministic `events.csv` fixtures and writes
  `solution.csv`, `peers.csv`, `logs.txt`, and optional truth comparison
  reports for regression/debug.
- Deterministic scenario generator produces `events.csv`, `truth.csv`, and
  `replay_config.csv` from JSON scenario files.
- Plotting tool generates replay diagnostics PNGs from `truth.csv`,
  `solution.csv`, `peers.csv`, and `compare_report.json`.
- Portable NMEA parser converts `$GPGGA`/`$GNGGA` and `$GPRMC`/`$GNRMC` byte
  streams into `nav_gnss_sample_t` for `NAV_EVT_LOCAL_GNSS_SAMPLE`.
- Real GNSS-to-core telemetry, full peer-pair TDMA ranging, endpoint-bearing
  pair range storage, UBX parsing, and FC/MAVLink output remain future work.

## Build And Test

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Run the host demo:

```bash
./build/ports/posix/nav_posix_demo
```

Run a replay fixture:

```bash
./build/tools/replay/nav_replay \
  --events examples/replay/radio_3d_success/events.csv \
  --truth examples/replay/radio_3d_success/truth.csv \
  --config examples/replay/radio_3d_success/replay_config.csv \
  --out-dir build/replay/radio_3d_success \
  --node-id 0 \
  --pretty
```

Replay auto-discovers `replay_config.csv` and `truth.csv` next to `events.csv`
unless disabled. It writes `solution.csv`, `peers.csv`, `logs.txt`, and, when
truth is present, `compare_report.txt` and `compare_report.json`. Run only
replay tests with:

```bash
ctest --test-dir build -V -R replay
```

Generate, replay, and plot a deterministic software scenario:

```bash
python tools/sim/generate_scenario.py \
  --scenario examples/scenarios/static_anchors_success.json \
  --out-dir build/generated/static_anchors_success \
  --run-replay ./build/tools/replay/nav_replay \
  --overwrite \
  --pretty

python tools/plot/plot_replay.py \
  --truth build/generated/static_anchors_success/truth.csv \
  --solution build/generated/static_anchors_success/replay/solution.csv \
  --peers build/generated/static_anchors_success/replay/peers.csv \
  --compare-report build/generated/static_anchors_success/replay/compare_report.json \
  --out-dir build/generated/static_anchors_success/plots \
  --pretty
```

The plot tool requires matplotlib:

```bash
python -m pip install -r requirements-dev.txt
```

Inspect a GNSS NMEA text log with the host dump tool:

```bash
./build/tools/gnss/nav_nmea_dump examples/gnss/valid_gga_rmc.nmea
```

## Repository Structure

```text
CHANGELOG.md     PR-level summary of notable project changes.
platformio.ini   PlatformIO project: nodemcu-32s, esp32-s3-devkitc-1, speedybee.
core/            Portable C11 navigation core, app modules, and public headers.
                 nav_core / nav_peer_table / nav_trilateration / nav_nmea,
                 plus nav_mock, nav_serial_json, nav_telemetry, nav_tdma.
include/, src/   ESP-IDF node firmware: drivers, boot self-test, NodeConfig
                 (NVS persistence), ControlChannel (USB-serial JSON).
ports/esp32s3/   ESP-IDF port notes.
ports/speedybee/ Arduino/ESP8266 SpeedyBee node firmware + SX1280 lib + ranging
                 reference firmware.
ports/posix/     Host demo of the single-node mock + control-app JSON flow.
control-app/     Single-file browser control app (Web Serial, desktop Chrome).
docs/            Architecture, data model, logging, replay, and protocol docs.
tools/           Host replay runner, NMEA dump, scenario generator, plotter.
tests/           Host C tests for core and app modules.
examples/        Replay/GNSS fixtures and committed deterministic scenarios.
```

### Control App

For the field-kit path, start the app through the OS launcher in `control-app/`
(`start-linux.sh`, `start-windows.bat`, or `start-macos.command`). The launcher
requires Python 3, starts a range-capable `localhost` server, and opens
`index.html`. Place the optional offline Kyiv map sidecar at
`control-app/kyiv-oblast.pmtiles`; it is not committed to git. Directly opening
`control-app/index.html` can still show the non-map UI or coordinate-plot
fallback.

In desktop Chrome or Edge, click Connect and pick the node's USB-serial port.
The app shows the connected node's mode/solution, live map position, peer
snapshot, and distance observations discovered from that serial stream. The map
uses blue/green markers for GNSS positions and amber/orange markers for accepted
`RADIO_3D` no-GPS estimates. The distance table is built from discovered node
IDs plus received `range_result` telemetry, including best-effort
`source=air_report` reports for pairs measured by other nodes. Valid ranges are
shown in green. Failed ranging attempts that still include an SX1280
`uncorrected_m` diagnostic, such as short-range `invalid distance`, show that
diagnostic distance in red. Missing or GPS-derived fields are shown as `—` until
that data exists.
The node-name controls cache labels in the browser and can persist the connected
node's name to device storage (NVS on ESP32, EEPROM on SpeedyBee).
The app also lets you set node ID, toggle the navigation core's GPS preference
(off = trilateration), toggle the mock peer source, and set the constant
altitude. In the current distance-only ESP32 firmware, the hardware GPS task is
not started regardless of that control setting.

The next radio/control update should move this from parsed log/report text into
structured serial JSON pair-range output (`from_id`, `to_id`, distance,
freshness, validity, diagnostics). The control channel talks newline-delimited
JSON (see `nav_serial_json`); Web Serial is desktop-only.

### Single-Node Development

The node injects synthetic peers from the mock source when mock is enabled, so a
single board produces a real trilateration solution without four nodes. Toggle it
from the control app. The same flow runs fully on the host:

```bash
cmake -S . -B build && cmake --build build
./build/ports/posix/nav_posix_demo
```

## Read First

- [Architecture](docs/architecture.md)
- [Data flow](docs/data_flow.md)
- [Data model](docs/data_model.md)
- [State machine](docs/state_machine.md)
- [Logging](docs/logging.md)
- [GNSS NMEA](docs/gnss_nmea.md)
- [Control app map field test](docs/control_app_map_field_test.md)
- [Debug playbook](docs/debug_playbook.md)
- [Radio protocol](docs/radio_protocol.md)
- [Replay CSV](docs/replay_csv.md)

## Replay Fixtures

- `examples/replay/radio_3d_success/events.csv`: expected final
  `RADIO_NAV_OK`, `RADIO_3D`, source `RADIO_3D`, reject `NONE`; includes
  `truth.csv` and comparison thresholds.
- `examples/replay/reject_not_enough_anchors/events.csv`: expected final
  `NOT_ENOUGH_ANCHORS`.
- `examples/replay/reject_missing_altitude/events.csv`: expected final
  `MISSING_LOCAL_ALTITUDE`.
- `examples/replay/reject_stale_range/events.csv`: expected final
  `NOT_ENOUGH_ANCHORS`, with logs showing `STALE_RANGE`.
- `examples/replay/reject_bad_position/events.csv`: expected final
  `NOT_ENOUGH_ANCHORS`, with logs showing `BAD_POSITION`.

## Next Milestones

1. Add a platform UART adapter that stamps parsed NMEA samples with system time.
2. Integrate full ESP32 TDMA radio task: telemetry slots in packet mode,
   scheduled peer-pair ranging slots through the SX1280 ranging engine.
3. Extend range events/protocol/control JSON from implicit `peer_id` ranges to
   endpoint-bearing `from_id` / `to_id` pair ranges.
4. Implement full radio protocol framing with COBS, CRC32, ACKs, and timeouts.
5. Expand plot diagnostics beyond PNGs and `plot_summary.json`.
