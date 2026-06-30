# sb24tx-ranging

SX1280 ranging bring-up firmware for two SpeedyBee Nano 2.4G / SB24TX-style
ESP8285 + SX1280 modules.

This is a hardware bring-up example. It intentionally lives outside `core/` and
does not change the navigation-core radio coprocessor boundary.

## Behavior

One firmware image supports both roles:

- Press the bind button during the first 5 seconds after boot to select
  `slave`.
- Do nothing during the 5-second window to select `master`.
- The master prints one log line for every ranging exchange.
- The slave can run powered only from 5V; LED patterns show basic state.

The v0 RF profile is fixed:

```text
frequency_hz=2445000000
spreading_factor=SF7
bandwidth=1625kHz
coding_rate=4/5
ranging_address=0x53423234
calibration=13528
```

The master prints `corrected_m` as the primary distance. `raw_m` and `raw_reg`
are still included because short SX1280 ranges below about 20 m need
application-specific correction.

Each successful line includes `correction=short_exp` or
`correction=linear_bias`:

- `short_exp`: `biased_raw_m <= 18.5`, so the Semtech-style short-range
  exponential curve is active.
- `linear_bias`: `biased_raw_m > 18.5`, so `corrected_m` is only
  `raw_m + raw_bias_m`; no short-range exponential correction is applied.

The current v0 also applies an empirical `raw_bias_m=6.2` before short-range
correction. This was chosen from a ~2 m SB24TX sample where raw results were
clustered around `-3.7 m`; the bias moves that cluster to the Semtech
short-range correction input that displays about 2 m. Keep it visible in logs
until more 2 m / 5 m / 10 m samples are collected.

## LED Patterns

- Role selection window: fast blink.
- Master selected: slow blink while ranging.
- Slave selected: double-blink heartbeat while listening/responding.
- Radio init failure: rapid blink forever.

## Build

From the repository root, using the root virtualenv:

```bash
python3 -m venv .venv
. .venv/bin/activate
python -m pip install platformio
platformio run -d examples/sb24tx-ranging
```

Flash one module, then flash the other:

```bash
. .venv/bin/activate
platformio run -d examples/sb24tx-ranging -t upload
```

The module has no USB. Connect a CP210x/USB-TTL adapter to the UART pads:

```text
CP210x TX -> RX pad (GPIO3)
CP210x RX <- TX pad (GPIO1)
CP210x GND -> GND
CP210x 5V  -> 5V
```

To enter the ESP8285 bootloader, hold the bind button while powering the board,
then release it before upload.

## Test Workflow

1. Flash the same firmware to both modules.
2. Power the remote module from 5V.
3. After normal boot, press the bind button within 5 seconds so it becomes
   `slave`.
4. Go back to the laptop about 2 m away.
5. Connect the second module through CP210x.
6. Let the 5-second role window expire so it becomes `master`.
7. Open the monitor:

```bash
. .venv/bin/activate
platformio -d examples/sb24tx-ranging device monitor
```

Successful master output looks like:

```text
range_result ok=true attempt=12 corrected_m=2.00 correction=short_exp short_limit_m=18.5 raw_m=-3.70 biased_raw_m=2.50 raw_bias_m=6.20 raw_reg=-164 rssi_dbm=-48 snr_db=8.0 elapsed_ms=24 pa_start=tx pa_result=rx pa_rx_switch_us=3000
```

For tests beyond 20 m, expect successful lines to switch to:

```text
correction=linear_bias
```

At that point `corrected_m` should track `biased_raw_m` directly. This is the
right regime for checking whether the 2 m empirical bias still holds at larger
distances.

Failure output looks like:

```text
range_result ok=false attempt=13 elapsed_ms=350 irq=0x0 flags="" pa_start=tx pa_result=rx note="timeout or missing response; check slave power and RF front-end path"
```

## RF Front-End Caveat

The SX1280 ranging engine automatically changes direction inside one exchange:
the master transmits then receives, and the slave receives then transmits. The
SpeedyBee board also has external `RXEN`/`TXEN` GPIOs for the RF front-end.

This firmware can switch the master from TX path to RX path after starting the
exchange, but it cannot perfectly switch the slave at the chip's automatic
response instant. If the logs show repeated timeouts or slave discards, treat
that first as an RF front-end path issue, not a distance-math issue.

The current v0 still logs every attempt clearly so this failure mode is visible.
