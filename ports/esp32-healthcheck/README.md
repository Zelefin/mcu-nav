# ESP32 Hardware Health Check

PlatformIO + Arduino firmware for checking the wiring and basic module
functionality of an ESP32 navigation-node hardware stack. It is a diagnostic
port only: it does not use `core/`, does not implement ranging, and does not
implement the future radio protocol.

## What It Checks

- E28-2G4M12SX / SX128x radio over SPI with RadioLib.
- Radio TX by periodically sending a short LoRa health packet.
- Radio RX by listening for matching packets from another board running this
  firmware.
- HGLRC M100-5883 GPS UART by receiving and parsing NMEA at 115200 baud.
- QMC5883 compass by writing configuration over I2C and reading raw XYZ data.
- Periodic FreeRTOS health summary on the serial console.

Radio summary is `WARN` until at least one TX succeeds and one peer packet is
received. With only one board powered, `RADIO=WARN` after successful TX is
expected because RX cannot be proven without a transmitter using the same LoRa
settings.

## Supported Boards

- NodeMCU-32S / NodeMCU-32S Lua
- ESP32-S3-DEVKITC-1

Board-specific pins are selected by the PlatformIO environment through
`build_flags`; firmware logic is shared by both targets.

## NodeMCU-32S Wiring

### E28-2G4M12SX / SX128x

| E28 signal | ESP32 GPIO |
| ---------- | ---------- |
| SCK | 18 |
| MISO | 19 |
| MOSI | 23 |
| NSS / CS | 5 |
| RST | 27 |
| BUSY | 26 |
| DIO1 | 25 |
| DIO2 | 33 |
| DIO3 | 32 |
| 3V3 | 3.3 V |
| GND | GND |

### HGLRC M100-5883 GPS

| GPS signal | ESP32 GPIO |
| ---------- | ---------- |
| TX | 16 RX |
| RX | 17 TX |
| VCC | 3.3 V preferred |
| GND | GND |

### QMC5883 Compass

| Compass signal | ESP32 GPIO |
| -------------- | ---------- |
| SDA | 21 |
| SCL | 22 |
| VCC | 3.3 V preferred |
| GND | GND |

## ESP32-S3-DEVKITC-1 Wiring

### E28-2G4M12SX / SX128x

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

### HGLRC M100-5883 GPS

| GPS signal | ESP32-S3 GPIO |
| ---------- | ------------- |
| TX | 18 RX |
| RX | 17 TX |
| VCC | 3.3 V preferred |
| GND | GND |

### QMC5883 Compass

| Compass signal | ESP32-S3 GPIO |
| -------------- | ------------- |
| SDA | 4 |
| SCL | 5 |
| VCC | 3.3 V preferred |
| GND | GND |

## Build And Upload

Run commands from this directory:

```bash
cd ports/esp32-healthcheck
```

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

## Expected Output

```text
[INFO] [SYSTEM] Booting firmware
[INFO] [SYSTEM] Board: NodeMCU-32S
[INFO] [SYSTEM] Build: Jun 30 2026 12:00:00
[INFO] [SYSTEM] Pin map:
[INFO] [RADIO] Initializing SX128x radio over SPI
[OK] [RADIO] SX128x initialized: freq=2445.0 MHz bw=812.5 kHz sf=7 cr=5 power=2 dBm
[OK] [RADIO] RX listen mode started
[OK] [RADIO] TX packet sent: bytes=39 payload="mcu-nav-health board=NodeMCU-32S seq=0"
[OK] [RADIO] RX packet received: bytes=47 RSSI=-48.5 dBm SNR=8.0 dB payload="..."
[INFO] [GPS] UART started at 115200 baud RX=16 TX=17
[OK] [GPS] NMEA data received: sentences=12 new_sentences=yes
[WARN] [GPS] Data received but no location fix yet satellites=0 hdop=0.00
[OK] [COMPASS] QMC5883 detected at 0x0D
[OK] [COMPASS] QMC5883 config write OK
[INFO] [COMPASS] Raw magnetic field: X=123 Y=-45 Z=890
[INFO] [SYSTEM] Health summary: RADIO=OK GPS=WARN COMPASS=OK | radio tx=1 rx=1 gps_bytes=530 gps_sentences=10 compass_xyz=123,-45,890
```

## Radio TX/RX Procedure

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

## Hardware Safety

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

## Troubleshooting

### No Radio Detected

- Check 3.3 V power and common GND.
- Check SCK/MISO/MOSI/CS wiring for the selected PlatformIO environment.
- Check RST and BUSY wiring; SX128x init can fail if either is wrong.
- Confirm the module is SX1280/SX1281-compatible.

### Radio BUSY Stuck HIGH

- Check that BUSY is connected to the configured GPIO.
- Check RST wiring and module power.
- Power-cycle the board and module together.

### Radio TX Works But RX Never Works

- Use two boards flashed with this same firmware.
- Keep both boards on the same radio build flags.
- Attach antennas.
- Put boards at least tens of centimeters apart to avoid near-field overload.
- Make sure both boards are powered long enough to transmit; default TX interval
  is 10 seconds.

### No GPS Bytes

- GPS TX must connect to ESP RX.
- Confirm `PIN_GPS_RX` for the active environment.
- Confirm GPS baud is 115200.
- Check GPS power and common GND.

### GPS Bytes Received But No Fix

- This is normal indoors or near windows with poor sky view.
- Move outdoors and wait for almanac acquisition.
- Check antenna orientation and module backup battery.

### Compass Not Found

- Confirm SDA/SCL are not swapped.
- Check 3.3 V power and common GND.
- Confirm pull-ups are present and not pulled to 5 V.
- Confirm the compass is QMC5883-compatible at address `0x0D`.

### Compass Found But Raw Data Is Suspicious

- Move or rotate the board and watch whether XYZ changes.
- Keep the compass away from USB cables, magnets, motors, and high-current
  wires during the check.

### Serial Monitor Empty On ESP32-S3

- This environment enables `ARDUINO_USB_MODE=1` and
  `ARDUINO_USB_CDC_ON_BOOT=1`.
- Press reset after opening the monitor.
- Confirm the monitor is attached to the USB CDC port, not an external UART.

### Wrong PlatformIO Board ID For ESP32-S3

If your PlatformIO install does not know `esp32-s3-devkitc-1`, list available
boards:

```bash
pio boards espressif32 | grep -i "s3.*devkit"
```

Then replace the `board = esp32-s3-devkitc-1` line in
`platformio.ini` with the board ID from your installed PlatformIO version.
