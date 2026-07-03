# ADR 0007: Telemetry-UI Network Map

## Status

**Accepted** (2026-07).

## Context

The telemetry UI shows the network view as numeric tables. We want to render node
positions spatially (the network map) from a single vantage node. Two choices are
not obvious to a future reader:

- The UI runs over Web Serial by "plugging into one node," often in the field
  where there may be no internet. An online tile map depends on internet for its
  backdrop.
- Positions reach the UI two ways with different meaning: this node's own *solved*
  position (`snapshot.pos`, valid only when `sol`/`src` are not `NONE`/`REJECTED`)
  and each peer's raw *GNSS* coordinate (`peers[].gnss` + `lat_e7/lon_e7`, which
  per ADR 0005 a peer only advertises when its GPS is enabled and freshly fixed).

## Decision

1. The network map uses Leaflet with OpenStreetMap raster tiles (via `react-leaflet`
   v4, pinned for React 18). No API key, no self-hosted tiles. When offline, tiles
   fail to load but markers still render on a blank backdrop.
2. The map is driven **only** by the peer snapshot — the same source as the
   existing "This Node" and "Peer Snapshot" tables. It does not depend on Debug
   telemetry mode or `node_quality` records.
3. This node is plotted from its solved `snapshot.pos`; peers are plotted from
   their advertised GNSS coordinates. This asymmetry is intentional and mirrors the
   tables; the map is not a trilateration engine.
4. A node is "positioned" only with a finite, non-`(0,0)` coordinate and, for this
   node, a valid solution (`src`/`sol` not `NONE`/`REJECTED`). Un-positioned nodes
   appear in an off-map roster, never at null island.

## Considered Options

- **Offline canvas/SVG projection** (no tiles): fully field-safe but no map
  backdrop — rejected as not an "online map" and less useful for bench bring-up.
- **Merging `node_quality` positions**: rejected — adds a second code path and a
  precedence rule while under ADR 0005 rarely surfacing positions the snapshot
  lacks.

## Consequences

- The map is blank (banner + roster only) until at least one node reports a usable
  GNSS coordinate or an accepted `RADIO_3D`/`LOCAL_GNSS` solution — expected during
  GPS bring-up when no node has a fix.
- Field deployments without internet get markers on a blank backdrop; a future
  offline-tiles option would be a separate decision.
