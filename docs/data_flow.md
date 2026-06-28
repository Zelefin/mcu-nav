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

## Range Update

```mermaid
sequenceDiagram
    participant Port
    participant Core
    participant PeerTable
    participant Log

    Port->>Core: NAV_EVT_RANGE_RESULT
    Core->>PeerTable: nav_peer_table_update_range()
    Core->>Log: RANGE range_update
    Core->>Core: attempt snapshot update
```

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
