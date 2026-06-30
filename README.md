# nav-mcu

Portable navigation-brain firmware for a group UAV navigation system.

This repository owns the main MCU navigation core, host tests, diagnostics,
documentation, and future host ports. It does not implement the ESP8285/SX1280
radio firmware, real GNSS hardware drivers, or flight-controller output.

## ESP32 Hardware Health Check

The repository root is also a PlatformIO + ESP-IDF firmware project for
checking the wiring and basic module functionality of an ESP32
navigation-node hardware stack. This firmware is a diagnostic only: it does not
use `core/`, does not implement ranging, and does not implement the future radio
protocol.

### What It Checks

- E28-2G4M12SX / SX128x radio over SPI with RadioLib.
- Radio TX by periodically sending a short LoRa health packet.
- Radio RX by listening for matching packets from another board running this
  firmware.
- HGLRC M100-5883 GPS UART with the ESP-IDF UART driver and a fixed-buffer NMEA
  parser at 115200 baud.
- QMC5883 compass with the ESP-IDF I2C driver by writing configuration and
  reading raw XYZ data.
- Periodic FreeRTOS health summary on the serial console.

Radio summary is `WARN` until at least one TX succeeds and one peer packet is
received. With only one board powered, `RADIO=WARN` after successful TX is
expected because RX cannot be proven without a transmitter using the same LoRa
settings.

### Supported Boards

- NodeMCU-32S
- ESP32-S3-DEVKITC-1

Board-specific pins are selected by the PlatformIO environment through
`build_flags`; firmware logic is shared by both targets.

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
t=...ms [INFO] [RADIO] Initializing SX128x radio over SPI
t=...ms [OK] [RADIO] SX128x initialized: freq=2445.0 MHz bw=812.5 kHz sf=7 cr=5 power=2 dBm
t=...ms [OK] [RADIO] RX listen mode started
t=...ms [OK] [RADIO] TX packet sent: bytes=39 payload="mcu-nav-health board=NodeMCU-32S seq=0"
t=...ms [OK] [RADIO] RX packet received: bytes=47 RSSI=-48.5 dBm SNR=8.0 dB payload="..."
t=...ms [INFO] [GPS] UART started at 115200 baud RX=16 TX=17
t=...ms [OK] [GPS] NMEA data received: sentences=12 new_sentences=yes
t=...ms [WARN] [GPS] Data received but no location fix yet satellites=0 hdop=0.00
t=...ms [OK] [COMPASS] QMC5883 detected at 0x0D
t=...ms [OK] [COMPASS] QMC5883 config write OK
t=...ms [INFO] [COMPASS] Raw magnetic field: X=123 Y=-45 Z=890
t=...ms [INFO] [SYSTEM] Health summary: RADIO=OK GPS=WARN COMPASS=OK | radio tx=1 rx=1 gps_bytes=530 gps_sentences=10 compass_xyz=123,-45,890
```

### Radio TX/RX Procedure

For a full radio check, flash this firmware to two boards with E28/SX128x
modules wired. Power both boards and monitor either serial console.

- TX is proven when `TX packet sent` appears.
- RX is proven when `RX packet received` appears.
- Both boards must use the same `RADIO_FREQUENCY_MHZ`,
  `RADIO_BANDWIDTH_KHZ`, `RADIO_SPREADING_FACTOR`, `RADIO_CODING_RATE`,
  `RADIO_SYNC_WORD`, and `RADIO_PREAMBLE_LEN` build flags.
- Default frequency is 2445 MHz. Keep antennas attached while transmitting.

This is a packet smoke test, not time-of-arrival, ranging, slotting, or the
future radio-coprocessor protocol.

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

- GPS TX must connect to ESP RX.
- Confirm `PIN_GPS_RX` for the active environment.
- Confirm GPS baud is 115200.
- Check GPS power and common GND.

#### GPS Bytes Received But No Fix

- This is normal indoors or near windows with poor sky view.
- Move outdoors and wait for almanac acquisition.
- Check antenna orientation and module backup battery.

#### Compass Not Found

- Confirm SDA/SCL are not swapped.
- Check 3.3 V power and common GND.
- Confirm pull-ups are present and not pulled to 5 V.
- Confirm the compass is QMC5883-compatible at address `0x0D`.

#### Compass Found But Raw Data Is Suspicious

- Move or rotate the board and watch whether XYZ changes.
- Keep the compass away from USB cables, magnets, motors, and high-current
  wires during the check.

#### Serial Monitor Empty On ESP32-S3

- ESP-IDF logs are written to the configured ESP-IDF console.
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
- Real hardware ports, UART drivers, radio firmware, UBX parsing, and
  FC/MAVLink output remain future work.

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
platformio.ini   PlatformIO ESP-IDF firmware project for ESP32 health checks.
include/         ESP-IDF health-check firmware public headers.
src/             ESP-IDF health-check firmware task implementations.
core/            Portable C11 navigation core and public headers.
ports/           Platform adapters. Only POSIX demo exists now.
docs/            Architecture, data model, logging, replay, and protocol docs.
tools/replay/    Deterministic CSV replay runner.
tools/gnss/      Host NMEA log dump tool using the portable parser.
tools/sim/       Deterministic scenario-to-replay-input generator.
tools/plot/      Replay-output PNG diagnostics.
tests/           Host C tests for implemented core modules.
examples/replay/ Deterministic replay fixtures.
examples/gnss/   Small NMEA parser fixtures.
examples/scenarios/ Committed deterministic scenario definitions.
examples/        Captured log examples and scenario/replay fixtures.
```

## Read First

- [Architecture](docs/architecture.md)
- [Data flow](docs/data_flow.md)
- [Data model](docs/data_model.md)
- [State machine](docs/state_machine.md)
- [Logging](docs/logging.md)
- [GNSS NMEA](docs/gnss_nmea.md)
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
2. Add ESP32-S3/STM32 host adapters without platform dependencies in `core/`.
3. Implement full radio protocol framing with COBS, CRC32, ACKs, and timeouts.
4. Expand plot diagnostics beyond PNGs and `plot_summary.json`.
