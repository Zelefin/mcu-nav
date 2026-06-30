# esp32s3-ranging

SX1280 ranging bring-up firmware for two ESP32-S3-DEVKITC-1 boards wired to
E28-2G4M12SX modules.

This is a hardware bring-up example. It is self-contained under
`examples/esp32s3-ranging`, uses RadioLib's SX1280 ranging API, and does not
change the root hardware health-check firmware or `core/`.

## Behavior

One firmware image supports both roles:

- Press BOOT/GPIO0 during the first 5 seconds after boot to select `slave`.
- Do nothing during the 5-second window to select `master`.
- The master prints one log line for every ranging attempt.
- The slave can run powered only from 5V after role selection; master logs are
  the primary evidence that it is responding.

Optional forced-role PlatformIO environments are also available for diagnostics:
`esp32-s3-devkitc-1-master` and `esp32-s3-devkitc-1-slave`.

The v0 RF profile is fixed:

```text
frequency_hz=2445000000
spreading_factor=SF7
bandwidth=1625kHz
coding_rate=4/5
ranging_address=0x53423234
calibration_sf7_bw1625=13528
```

The master prints `uncorrected_m` as the primary distance. This is RadioLib's
SX1280 conversion without the empirical `<20m` short-range correction used by
`examples/sb24tx-ranging`.

## Wiring

Use the same ESP32-S3 wiring as the repository-root health-check firmware.

| E28 signal | ESP32-S3 GPIO |
| ---------- | ------------- |
| SCK | 12 |
| MISO | 13 |
| MOSI | 11 |
| NSS / CS | 10 |
| RST | 9 |
| BUSY | 8 |
| DIO1 | 7 |
| 3V3 | 3.3 V |
| GND | GND |

This example does not use external PA/LNA RXEN or TXEN pins.

## Build

From the repository root, using the local virtualenv:

```bash
.venv/bin/pio run -d examples/esp32s3-ranging
```

If you need to create the virtualenv first:

```bash
python3 -m venv .venv
.venv/bin/python -m pip install platformio intelhex
```

## Flash And Monitor

Flash one board, then flash the other:

```bash
.venv/bin/pio run -d examples/esp32s3-ranging -t upload --upload-port /dev/ttyACM0
```

To remove BOOT-button role selection from a test, flash explicit roles:

```bash
.venv/bin/pio run -d examples/esp32s3-ranging -e esp32-s3-devkitc-1-slave -t upload --upload-port /dev/ttyACM0
.venv/bin/pio run -d examples/esp32s3-ranging -e esp32-s3-devkitc-1-master -t upload --upload-port /dev/ttyACM0
```

Open the monitor:

```bash
.venv/bin/pio device monitor -p /dev/ttyACM0 -b 115200
```

For the ESP32-S3 USB CDC/JTAG port, opening the serial reader after upload can
miss one-shot startup logs. The slave emits repeated listen logs, so read it
directly after upload:

```bash
stty -F /dev/ttyACM0 115200 raw -echo -hupcl
cat /dev/ttyACM0
```

Pressing RESET can make `cat` exit when USB CDC re-enumerates; if that happens,
run the `stty` and `cat` commands again. For a forced slave build, expect:

```text
role_select selected=slave reason=build_flag
slave_ready arming_delay_ms=5000 ...
slave_arming remaining_ms=...
slave_listen start role=slave timeout_ms=10000
```

You can also upload and monitor in one command:

```bash
.venv/bin/pio run -d examples/esp32s3-ranging -t upload -t monitor --upload-port /dev/ttyACM0 --monitor-port /dev/ttyACM0
```

If logs do not appear after upload, open the monitor and press RESET.

## Test Workflow

1. Flash the same firmware to both ESP32-S3 boards.
2. Power or reset the remote board.
3. During the first 5 seconds after boot, press BOOT so it becomes `slave`.
4. Leave the slave powered from 5V.
5. Connect the second board to the laptop.
6. Let the 5-second role window expire so it becomes `master`.
7. Open the serial monitor and watch the master logs.

Successful master output looks like:

```text
range_result ok=true role=master attempt=12 uncorrected_m=3.42 raw_reg=123 rssi_dbm=-48.5 snr_db=8.0 elapsed_ms=24
```

Failure output looks like:

```text
range_result ok=false role=master attempt=13 elapsed_ms=350 error=-901 irq=0x0400 flags="master_timeout" note="ranging timeout; check slave role, power, wiring, address, and RF profile"
```

## Accuracy Caveat

This example proves SX1280 ranging exchanges. It does not promise calibrated
distance accuracy.

RadioLib's uncorrected distance is useful for bring-up, but real accuracy needs
known-distance samples and calibration. Keep `raw_reg` in logs so later
correction or calibration work can compare against the raw SX1280 result.
