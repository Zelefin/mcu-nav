# radio-range — SpeedyBee Nano 2.4G band scanner

PlatformIO firmware that turns a **SpeedyBee Nano 2.4G** ExpressLRS receiver
(**ESP8285 + Semtech SX1280**) into a live 2.4 GHz "band load" scanner. It
steps the SX1280 across the ISM band, reads the instantaneous receiver RSSI at
each step, and prints an ASCII waterfall over UART so you can see congestion.

> Not a calibrated spectrum analyzer — readings are relative RSSI. Good for
> finding busy/quiet channels (WiFi, Bluetooth, video TX), not absolute power.

## Hardware

Pin map is taken from `ExpressLRS/targets → RX/Generic 2400 PA.json`
(product `SpeedyBee Nano 2.4GHz RX`, platform `esp8285`). See
[include/board_pins.h](include/board_pins.h).

| Signal | GPIO | Signal      | GPIO |
|--------|------|-------------|------|
| NSS    | 15   | RST         | 2    |
| SCK    | 14   | BUSY        | 5    |
| MOSI   | 13   | DIO1        | 4    |
| MISO   | 12   | PA RX_EN    | 9    |
| UART RX| 3    | PA TX_EN    | 10   |
| UART TX| 1    | LED / BOOT  | 16/0 |

## Build & flash

```bash
~/.platformio/penv/bin/pio run                 # compile
~/.platformio/penv/bin/pio run -t upload       # flash over UART (FTDI)
~/.platformio/penv/bin/pio device monitor      # watch the scan @115200
```

The Nano has no USB. Wire an FTDI/USB-TTL adapter to the RX pads:

```
FTDI TX  -> RX pad (GPIO3)
FTDI RX  <- TX pad (GPIO1)
FTDI GND -> GND
FTDI 5V  -> 5V     (or 3V3 -> 3V3)
```

**Bootloader entry:** ESP8285 enters flash mode when GPIO0 (the bind button)
is held LOW at power-up. Hold the bind button while plugging in the FTDI, then
release and run `pio run -t upload`.

Alternatives that avoid the boot-button dance:
- **WiFi**: flash any ELRS build once, then OTA — out of scope here.
- **Betaflight passthrough**: if the RX is on a flight controller, use
  `pio run -t upload` with the FC's MSP passthrough.

## Output

```
  2.4 GHz band load  [2400 - 2483 MHz, 84 x 1 MHz bins]
  legend: ' '=quiet  . : - = + * # % @ =busy
    |         |         |         |         |  ...
    ..:-=*#%@#*=-..   ...:--:..        ..=*#%@%#*=...   peak 2437 MHz @ -52 dBm
```

Each row is one full sweep; columns are 1 MHz bins from 2400→2483 MHz.

## Tuning the scan

Edit the constants at the top of [src/main.cpp](src/main.cpp):
`F_START_MHZ`, `F_STOP_MHZ`, `F_STEP_MHZ`, `SETTLE_US`, `SAMPLES`, and the
`RSSI_FLOOR`/`RSSI_CEIL` display range. The RX bandwidth passed to
`radio.begin()` should roughly match `F_STEP_MHZ` (1 MHz step ↔ 1.2 MHz BW).
