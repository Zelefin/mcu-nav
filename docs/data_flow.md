# Data Flow

The core receives explicit events and emits snapshots/logs. It does not poll
drivers or own platform resources.

## Telemetry Update

```mermaid
sequenceDiagram
    participant Port
    participant Core
    participant PeerTable
    participant Log

    Port->>Core: NAV_EVT_PEER_TELEMETRY_RX
    Core->>PeerTable: nav_peer_table_update_beacon_rx()
    Core->>Log: PEER_TABLE telemetry_update
    Core->>Core: attempt snapshot update
```

`NAV_EVT_PEER_TELEMETRY_RX` carries `nav_peer_beacon_rx_t`: peer telemetry from
the remote node plus local receive metadata (`rssi_dbm`, `snr_db`).

## Local GNSS NMEA Update

Future UART ports feed bytes into the portable NMEA parser and wrap emitted
samples as normal core events. The parser does not own the UART or clock; the
adapter stamps `timestamp_ms` with local system/replay time before injection.

```mermaid
flowchart LR
    UART[GNSS UART / NMEA log] --> PARSER[nav_nmea_parser_push]
    PARSER --> SAMPLE[nav_gnss_sample_t]
    SAMPLE --> EVENT[NAV_EVT_LOCAL_GNSS_SAMPLE]
    EVENT --> CORE[nav_core]
    CORE --> SNAP[nav_snapshot_t]
```

When `demo_force_gps_denied` is true, the core stores/logs local GNSS samples
but does not use them as `NAV_SOURCE_LOCAL_GNSS`.

## Range Update

Distance measurements are produced by the SX1280 ranging engine in a scheduled
TDMA ranging slot. The platform radio driver converts the readable ranging
result into a range event. The current host/replay event uses an implicit local
endpoint plus `peer_id`; the ESP32 TDMA integration must carry explicit
`from_id` / `to_id` endpoints so third-party pair ranges can be displayed by the
control app. RSSI/SNR remain diagnostics; they are not distance inputs.

```mermaid
sequenceDiagram
    participant Port as ESP32 radio port
    participant SX as SX1280 ranging engine
    participant Core
    participant PeerTable
    participant PairRanges as Pair range view
    participant Log

    Port->>SX: start ranging master/slave for scheduled pair
    SX-->>Port: range_mm or radio failure
    Port->>Core: RANGE_RESULT(from_id, to_id, request_id)
    alt local node is one endpoint
        Core->>PeerTable: update local-to-peer anchor range
    else local node is neither endpoint
        Core->>PairRanges: update third-party pair observation
    end
    Core->>Log: RANGE range_update
    Core->>Core: attempt snapshot update
```

Third-party pair observations are for network health, diagnostics, replay, and
`control-app/`. They do not make a peer usable as an anchor for the local solver
unless one endpoint of the range is the local node.

The current ESP32 distance-only firmware implements this as a bring-up path:
after each local SX1280 ranging attempt, the ranging master broadcasts a compact
best-effort `range_result` text report. Nodes that hear it log the same
endpoint-bearing observation with `source=air_report`, allowing the control app
connected to one node to show pairs measured by other nodes. This report path is
diagnostic only and is not the final replay/core endpoint-bearing contract.

## Radio Navigation Solve Attempt

```mermaid
sequenceDiagram
    participant Core
    participant Select as Anchor selection
    participant Tril as nav_trilateration
    participant Snap as nav_snapshot
    participant Log

    Core->>Select: nav_select_radio_anchors(now_ms)
    Select-->>Core: 3 anchors or reject reason
    Core->>Core: resolve local altitude
    Core->>Core: compute triangle-area geometry score
    Core->>Tril: nav_trilat_solve_3_anchor_altitude()
    Tril-->>Core: lat/lon/alt + residuals or failure
    Core->>Snap: RADIO_3D / DEGRADED / REJECTED
    Core->>Log: solve_attempt / solve_succeeded / solve_rejected
```

## Anchor Rejection Path

```mermaid
flowchart TD
    PEER[Peer slot] --> PRESENT{present?}
    PRESENT -- no --> REJECT1[STALE_TELEMETRY]
    PRESENT -- yes --> TEL{telemetry fresh?}
    TEL -- no --> REJECT1
    TEL -- yes --> RNG{range fresh and valid?}
    RNG -- no --> REJECT2[STALE_RANGE]
    RNG -- yes --> GNSS{peer GNSS valid?}
    GNSS -- no --> REJECT3[BAD_GNSS]
    GNSS -- yes --> SIGMA{range sigma within config?}
    SIGMA -- no --> REJECT4[BAD_RANGE_SIGMA]
    SIGMA -- yes --> ACCEPT[anchor accepted]
```

## Successful Solution Path

```mermaid
flowchart LR
    A[3 accepted anchors] --> G[geometry area check]
    ALT[fresh local altitude] --> SOLVE[trilateration]
    G --> SOLVE
    SOLVE --> RES[residual diagnostics]
    RES --> OK[RADIO_NAV_OK + RADIO_3D]
```

## Replay Flow

`nav_replay` is a host-side tool. It owns CSV parsing, file I/O, output
directories, and replay logs. The portable core still only sees `nav_event_t`
values and emits snapshots/log callbacks.

```mermaid
flowchart LR
    CSV[events.csv] --> PARSER[Replay CSV parser]
    PARSER --> EVT[nav_event_t]
    EVT --> CORE[nav_core]
    CORE --> SNAP[nav_snapshot_t]
    CORE --> LOG[nav_logger_t]
    TRUTH[truth.csv optional] --> CMP[truth comparison]
    SNAP --> SOL[solution.csv]
    SNAP --> CMP
    CMP --> REPORT[compare_report.txt/json]
    CORE --> PEERS[peers.csv]
    LOG --> TXT[logs.txt]
```

Replay emits `solution.csv` and `peers.csv` after every processed input row.
Parser errors include the input line number and return a nonzero exit code.
When `truth.csv` exists, replay compares exact `(time_ms,node_id)` solution rows
against truth and writes comparison reports. It does not interpolate.

## Simulation And Plot Flow

The simulator layer is outside `core/` and outside `nav_replay`. It generates
deterministic replay inputs, then the existing replay runner remains the path
that drives the navigation core for offline scenario results.

```mermaid
flowchart LR
    SCN[scenario.json] --> GEN[generate_scenario.py]
    GEN --> EVT[events.csv]
    GEN --> TRUTH[truth.csv]
    GEN --> CFG[replay_config.csv]
    EVT --> REPLAY[nav_replay]
    CFG --> REPLAY
    TRUTH --> REPLAY
    REPLAY --> SOL[solution.csv]
    REPLAY --> PEERS[peers.csv]
    REPLAY --> REPORT[compare_report]
    SOL --> PLOT[plot_replay.py]
    PEERS --> PLOT
    TRUTH --> PLOT
    REPORT --> PLOT
    PLOT --> PNG[diagnostic PNGs]
```

`tools/sim/generate_scenario.py` writes `events.csv`, `truth.csv`,
`replay_config.csv`, and `scenario_resolved.json`. Optional seeded range noise,
explicit packet drops, and local-altitude cadence are deterministic.
`tools/plot/plot_replay.py` reads only replay-visible files: `truth.csv`,
`solution.csv`, `peers.csv`, and `compare_report.json`. It writes diagnostic
PNGs plus `plot_summary.json`.

## Replay Event Sequence

```mermaid
sequenceDiagram
    participant CSV as events.csv
    participant Replay as nav_replay
    participant Core as nav_core
    participant Out as output CSV/logs

    CSV->>Replay: parse row
    Replay->>Core: nav_core_tick(time_ms) if time advanced
    Replay->>Core: nav_core_handle_event(nav_event_t)
    Core-->>Replay: structured nav_logger_t callbacks
    Replay->>Out: append logs.txt
    Replay->>Core: nav_core_get_snapshot()
    Replay->>Out: append solution.csv and peers.csv
```

`packet_seq` in replay input is the remote peer beacon sequence. `request_id` is
the ranging correlation id. Replay row order is only file order.
