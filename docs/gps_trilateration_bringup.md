# GPS Trilateration Bring-up

Runbook status: not executed in this session. This procedure requires four real
nodes and is the repeat path for issue
`docs/issues/phase2_real_hardware_navigation/0007-radio-navigation-acceptance-on-real-boards.md`.

## Purpose

Prove that a GPS-disabled local node can accept a real `RADIO_3D` navigation
solution from over-the-air peer GNSS telemetry, fresh SX1280 ranges, and a fresh
local altitude source.

This is not the single-board GPS UART bring-up. A single NodeMCU-32S can prove
NMEA bytes, a local GNSS fix, `LOCAL_GNSS` snapshots, and GNSS-valid beacon TX,
but it cannot prove a real three-anchor radio solve.

## Required Hardware

- Four navigation nodes flashed from this repository.
- At least three anchor nodes with valid GNSS fixes and usable sky view.
- SX1280 radios and antennas attached on all nodes.
- One local node selected as the GPS-denied node under test.
- The control app or a serial monitor connected to the local node.

## Firmware Setup

1. Build and flash the firmware for each board type:

```bash
pio run -e nodemcu-32s
pio run -e esp32-s3-devkitc-1
pio run -e speedybee
```

2. Assign unique node IDs `0..3` over the control channel:

```json
{"cmd":"node_id","id":0}
{"cmd":"node_id","id":1}
{"cmd":"node_id","id":2}
{"cmd":"node_id","id":3}
```

3. Keep GPS use enabled on at least three anchor nodes:

```json
{"cmd":"gps","enabled":true}
```

4. For the current same-height field display test, the ESP field build flattens
   radio-solver altitude and leaves the default local altitude at `0 mm`.
   Only set altitude manually when intentionally testing non-default altitude:

```json
{"cmd":"alt","alt_mm":183500}
```

5. Start a control-app NDJSON recording, then disable GPS use on the local node
   under test:

```json
{"cmd":"gps","enabled":false}
```

## Evidence To Capture

On each anchor node, capture a GNSS-valid health line or NDJSON snapshot showing
a usable fix:

```text
GNSS fix ok ... usage=enabled
```

On the local node, capture:

- `snapshot` records where `data.solution_source` is not `LOCAL_GNSS` after
  GPS is disabled.
- At least three peer rows with valid GNSS coordinates inside the 30 second
  field evidence window.
- At least three local-endpoint `range` records from SX1280 ranging inside the
  30 second field evidence window.
- A local altitude source. For the same-height field scenario this may be the
  default `0 mm` value.
- Structured quality diagnostics with `num_anchors >= 3`.
- The accepted solution:

```json
{"solution_source":"RADIO_3D"}
```

Also capture the residual and geometry diagnostics:

- `residual_rms_m`
- `max_residual_m`
- `geometry_score`
- `anchor_ids`

When local GPS use is disabled, keep the GPS receiver connected. Snapshots
should include `local_gnss.used=false` and `local_gnss.usage="disabled"` so the
field recording can compare the accepted `RADIO_3D` position against the node's
real GNSS evidence without letting GNSS become the navigation solution.

## Expected Result

With three GNSS-valid anchors and three local-endpoint ranges inside the field
evidence window,
the GPS-disabled local node should report:

- `solution_status` equal to `RADIO_3D`
- `solution_source` equal to `RADIO_3D`
- `reject_reason` equal to `NONE`
- residual and geometry diagnostics present in snapshots and logs

For this bring-up mode, residual thresholds are intentionally permissive so a
rough `RADIO_3D` position can be displayed before range calibration is complete.
Ranging failures are still logged, but they do not erase the last successful
local-endpoint range until its 30 second evidence window expires.

If the solve is rejected, use `docs/debug_playbook.md` to separate navigation
`reject_reason` values such as `NOT_ENOUGH_ANCHORS`, `STALE_RANGE`,
`MISSING_LOCAL_ALTITUDE`, `BAD_GEOMETRY`, and `RANGE_OUTLIER` from radio-layer
`range_fail_reason` values such as `TIMEOUT` or `RADIO_BUSY`.

## Not Executed Here

This runbook was not executed because this session did not have a four-node
bench with at least three GNSS-valid anchors.
