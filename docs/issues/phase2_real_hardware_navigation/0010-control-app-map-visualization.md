# Control App Map Visualization

## What to build

Expand `control-app/index.html` into a live field visualization surface for the
four-node outdoor test: three GPS/GNSS nodes around one GPS-disabled node that
appears from an accepted `RADIO_3D` estimate.

This issue prepares the visual and control-data path. It does not implement the
ESP32 GNSS UART adapter, compass support, new trilateration math, browser
geolocation, multi-serial control, range-link overlays, anchor-triangle
overlays, or flight-controller output.

The app remains entered through `control-app/index.html`, but the field kit may
include sidecar assets: committed pinned MapLibre/PMTiles browser assets,
OS-specific Python 3 launchers, and a git-ignored Kyiv PMTiles bundle at
`control-app/kyiv-oblast.pmtiles`.

## Data contract

Serial/control JSON must expose UI-facing mappable-position fields for the
local node and peers:

- `position_source`: `GNSS`, `RADIO_3D`, or `NONE`
- `position_valid`: whether the control app may render a live marker
- `position_degraded`: whether the accepted position should use degraded styling
- reported position fields including `lat_e7`, `lon_e7`, and `alt_mm`
- GNSS validity for anchor eligibility, kept separate from `position_valid`
- timestamp/freshness information sufficient for stale marker handling

Peer telemetry should carry the source/status metadata needed to produce those
control JSON fields. A node with local GNSS appears as a GNSS position. A node
with no usable local GPS but an accepted `RADIO_3D` solution appears as an
estimated no-GPS position. Rejected or no-solution nodes do not appear as live
map markers.

## Map behavior

- The map is the primary live field surface above the existing distance and
  peer tables.
- The connected USB-serial node is the `this`/`me` marker. Browser/laptop
  geolocation is not used.
- GNSS positions render as blue/green markers with existing default labels such
  as `node-3`.
- Accepted `RADIO_3D` positions render as amber/orange markers with labels such
  as `node-1 (no GPS)`.
- Accepted degraded radio positions remain displayable with degraded styling.
- Recently stale positions remain briefly as faded markers with age/details,
  then leave the live map while remaining diagnosable in tables/logs.
- Altitude is hidden from map labels/overlays but remains visible in tables,
  debug details, serial JSON, logs, and replay outputs.
- The first map view shows node dots only. Range links, anchor triangles, and
  geometry overlays are out of scope.
- The map initially fits or follows visible markers. User pan or zoom pauses
  automatic camera movement until the operator presses a recenter control.
- The app keeps one USB-serial connection; other nodes appear through the
  connected node's radio-derived network view.

## Offline field kit

- Use MapLibre GL with the PMTiles protocol for the offline vector basemap.
- Commit small pinned JS/CSS browser assets needed by `index.html`; do not rely
  on CDNs at runtime.
- Do not commit the full Kyiv PMTiles bundle.
- Add tooling to download or build the Kyiv city + Kyiv oblast vector
  street/topographic PMTiles bundle with a small margin.
- The final field-kit map path is `control-app/kyiv-oblast.pmtiles`.
- Add or update `.gitignore` so generated PMTiles/map-bundle artifacts are not
  committed accidentally.
- Add Linux, Windows, and macOS launchers:
  - `start-linux.sh`
  - `start-windows.bat`
  - `start-macos.command`
- Launchers require Python 3, start a range-capable local `localhost` server,
  and open the browser app. Directly opening `index.html` may degrade to
  non-map UI or a coordinate-plot fallback.

## Test fixture

Add a deterministic control-app fixture for the four-node map state:

- three nodes with `position_source = GNSS` and `position_valid = true`
- one middle node with `position_source = RADIO_3D`,
  `position_valid = true`, and no usable GNSS
- no browser geolocation
- no range-link or triangle overlays

This fixture is the visual contract before real GNSS firmware is available.

## Acceptance criteria

- [ ] Serial/control JSON exposes `position_source`, `position_valid`, and
      `position_degraded` for local and peer mappable positions.
- [ ] `gnss_valid` remains separate from `position_valid`; estimated peer
      positions are displayable but are not treated as navigation anchors.
- [ ] The control app renders the map as the primary live field surface and
      keeps existing node controls, distance observations, peer table, and
      serial log available.
- [ ] GNSS markers are blue/green and use existing node labels.
- [ ] Accepted `RADIO_3D` markers are amber/orange and include `(no GPS)` in
      the label.
- [ ] Rejected or no-solution nodes do not appear as live markers.
- [ ] Degraded accepted radio positions remain displayable with degraded
      styling.
- [ ] Altitude is absent from map labels/overlays but present in diagnostics.
- [ ] The map shows node dots only; no range-link, triangle, or geometry
      overlays are added in this branch.
- [ ] Auto-fit/follow and recenter behavior works without fighting manual pan or
      zoom.
- [ ] The app works with one connected serial node and uses that node's network
      view for peers.
- [ ] Offline field-kit assets are local and deterministic: no CDN or online
      tile dependency is required for the offline path.
- [ ] Tooling downloads or builds `control-app/kyiv-oblast.pmtiles` for field
      use, while normal tests do not require the full Kyiv bundle.
- [ ] Linux, Windows, and macOS launchers start a Python 3 range-capable local
      server and open the app.
- [ ] An offline automated browser smoke test loads the deterministic fixture,
      verifies nonblank rendering, expected marker labels/styles, no altitude in
      map labels, and captures a screenshot artifact.
- [ ] `docs/control_app_map_field_test.md`, data model docs, radio/control
      protocol docs, and README links stay synchronized with the implementation.

## Blocked by

- Pair Range Network View for full real-peer network visibility.

## Related but out of scope

- Real GNSS Adapter To Core
- ESP32 TDMA Telemetry Path
- ESP32 SX1280 Ranging Slots
- Radio Navigation Acceptance On Real Boards
