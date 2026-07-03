# Implementation Plan: Telemetry-UI Network Map

A simple online map in `telemetry-ui/` that plots each node's position as seen
from the single connected vantage node. See ADR
[0007](adr/0007-telemetry-ui-network-map.md) and the **Network map** glossary
entry in [CONTEXT.md](../CONTEXT.md).

## Decisions (from design review)

| # | Decision | Choice |
|---|----------|--------|
| 1 | Map purpose | Geographic lat/lon map (absolute coordinates) |
| 2 | Position source | Peer snapshot only (`snapshot.pos` + peer `position_source`/`position_valid`); no Debug mode |
| 3 | Library | Leaflet + OpenStreetMap raster tiles |
| 4 | React integration | `react-leaflet` v4 (React 18) |
| 5 | Placement | Separate **Map** tab; full-width content area |
| 6 | Un-positioned nodes | Off-map roster + centered "waiting for fix" banner |
| 7 | Camera | Center/fit once on first position, then manual + a **Fit** button |
| 8 | Markers | Color-coded `divIcon` dots, always-on labels, click popup with details |
| 9 | Range lines | Yes — polyline this-node→positioned peer, labeled measured range + delta vs map distance |
| 10 | Testing | Pure `snapshotToMapModel` transform unit-tested hard; one shallow `MapView` smoke test with Leaflet mocked |

## Positioned vs un-positioned (correctness rules)

Mirror the existing table logic in [App.tsx](../telemetry-ui/src/App.tsx)
(`formatPosition`, `snapshotPeerHasTelemetry`, `formatDeg`).

- **This node** is positioned iff `snapshot.pos` has finite `lat_e7`/`lon_e7`
  **and** `snapshot.src` is not `NONE`, **and** `snapshot.sol` is not `NONE` /
  `REJECTED`. (Same guard as `formatPosition`.)
- **A peer** is positioned iff `peer.position_valid === true` **and**
  `lat_e7`/`lon_e7` are finite **and not both zero** (guard against the `(0,0)`
  no-fix sentinel — null island). `peer.gnss === true` / `position_source ===
  GNSS` means the coordinate can also be an anchor; `position_source ===
  RADIO_3D` means display-only.
- Coordinates are E7 degrees: `deg = e7 / 1e7`. Altitude is mm: `m = mm / 1000`.
- Everything else (including SpeedyBee, which never has GPS) goes to the roster.

## New files

- `telemetry-ui/src/lib/mapModel.ts` — pure transform + types (no React/Leaflet).

  ```ts
  export interface MapNode {
    id: number;
    label: string;          // name or `node-<id>`
    isSelf: boolean;
    lat: number; lon: number; altM: number | null;
    // peers only:
    rangeM: number | null; rssi: number | null; snr: number | null; quality: number | null;
    // self only:
    solutionSource: string | null; solutionStatus: string | null;
  }
  export interface RangeLine {
    fromId: number; toId: number;      // self -> peer
    measuredM: number;                 // from peer.range_mm
    endpoints: [[number, number], [number, number]]; // [self, peer] latlng
  }
  export interface MapModel {
    self: MapNode | null;
    peers: MapNode[];                  // positioned peers only
    unpositioned: { id: number; label: string; reason: string }[];
    lines: RangeLine[];                // only when both endpoints positioned
  }
  export function snapshotToMapModel(
    snapshot: SnapshotPayload | null,
    currentNodeId: number | null,
    labelFor: (id: number) => string,
    discoveredIds: number[],
  ): MapModel;
  ```

  The transform takes `discoveredIds` so nodes known to the app but absent/without
  position still appear in the roster.

- `telemetry-ui/src/components/MapView.tsx` — `react-leaflet` render of a `MapModel`:
  `MapContainer` + OSM `TileLayer` + `Marker`s (`L.divIcon`) + `Polyline`s +
  roster/banner overlay + Fit button. Props: `{ model: MapModel }`.

- `telemetry-ui/src/lib/mapModel.test.ts` — unit tests for the transform.

## Changes to existing files

- `telemetry-ui/package.json` — add deps `leaflet`, `react-leaflet@^4`; devDep
  `@types/leaflet`.
- `telemetry-ui/src/App.tsx`
  - Add `activeTab` state (`"tables" | "map"`, default `"tables"`).
  - Add a tab strip at the top of the content area; render the existing
    `content-grid` for Tables, `<MapView>` for Map.
  - Build the model with `useMemo(() => snapshotToMapModel(snapshot, currentNodeId,
    labelFor, discoveredIds), [...])`.
  - **Mount `MapView` only when the Map tab is active** (conditional render, not
    `display:none`) so Leaflet initializes into a sized, visible container and
    avoids `invalidateSize` gymnastics.
- `telemetry-ui/src/styles.css` — tab strip styles; `.map-panel { height: … }`
  (Leaflet requires an explicit container height); marker-dot / label / popup /
  roster / banner styles; self-vs-peer dot colors.
- `telemetry-ui/src/main.tsx` (or `styles.css`) — `import "leaflet/dist/leaflet.css"`.

## Marker / line / camera behavior

- **Self dot** and **peer dot** use distinct colors (`divIcon`, so no PNG-asset
  path issue under Vite). Label = node label + `#id`; self label suffixed `(this)`.
- **Popup**: self → lat/lon, alt, solution source/status. Peer → lat/lon, alt,
  measured distance, RSSI/SNR, quality.
- **Range line**: for each positioned peer with a valid `range_mm`, a polyline
  self→peer labeled `<measured> m` and `Δ <measured − geodesic> m` (geodesic via
  Leaflet `latlng.distanceTo`).
- **Camera**: on the first render where ≥1 node is positioned, `fitBounds` to all
  positioned markers (with padding); afterwards never auto-move. A **Fit** button
  re-runs `fitBounds` on demand. Track "have we fit yet" in a ref.
- **Empty state**: when `self == null && peers.length === 0`, show a centered
  banner ("No positioned nodes yet — waiting for a GPS fix or RADIO_3D solve") over
  a default region view; roster lists all discovered nodes with reasons.

## Testing

- `mapModel.test.ts` (vitest): E7→deg conversion; `(0,0)` peer excluded → roster;
  peer with `position_valid:false` excluded; peer with `position_source:RADIO_3D`
  included as display-only; self excluded when `src=NONE`/`sol=REJECTED`; self
  included when solution valid; range line built only when both endpoints
  positioned; measured range from `range_mm`; roster covers discovered-but-absent
  nodes; SpeedyBee with no mappable solution stays in the roster. Reuse
  `fixtures/sample_capture.ndjson` shapes where useful.
- One `MapView` smoke test (`@testing-library/react`) with `react-leaflet` mocked
  to trivial stubs: asserts roster/banner render for an empty model and marker
  count for a populated model. No assertions on Leaflet internals.
- `npm run build` (tsc) and `npm test` must pass.

## Out of scope (follow-ups)

- Offline/self-hosted tiles for internet-less field use (ADR 0007 notes this as a
  separate decision).
- Last-known-position "ghost" markers for nodes that went stale.
- Merging `node_quality` (Debug-mode) positions.
- Track history / breadcrumb trails.

## Verification

Because no node currently holds a GPS fix, verify against a **capture replay**:
open a capture (or hand-craft an NDJSON) whose snapshot has at least one peer
with `position_valid:true`, non-zero coords, and a valid `snapshot.pos`; confirm
markers, labels, popup, range line, and Fit; then confirm the all-no-position
capture shows banner + full roster.
