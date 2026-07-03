# Control App Map Field Test

This checklist verifies the control-app visualization path for the four-node
outdoor scenario. It does not require this branch to implement the ESP32 GNSS
UART adapter.

## Field Kit

- Build or prepare the control-app field kit with `control-app/index.html`,
  local vendored map assets, launcher scripts, and the offline PMTiles sidecar.
- Place the Kyiv offline map at the fixed sidecar path:
  `control-app/kyiv-oblast.pmtiles`.
- Start the app with the OS launcher:
  - Linux: `start-linux.sh`
  - Windows: `start-windows.bat`
  - macOS: `start-macos.command`
- The launcher starts the Python 3 range-capable `localhost` server and opens
  the browser app.

## Setup

- Use four nodes with stable IDs and default node labels such as `node-0`.
- Connect the control app to one node over USB serial.
- Keep only one serial connection open; the other nodes must appear through the
  connected node's radio-derived network view.
- Start an NDJSON recording before moving nodes apart. Stop it after the test
  segment; the browser downloads a `nav-mcu-node-*.ndjson` capture.
- Enable debug telemetry only during bring-up or diagnosis, then turn it off
  when checking final ranging/trilateration behavior.
- Place three GPS-enabled nodes around the test area as anchors.
- Place the GPS-disabled node near the middle of the triangle.
- Keep modules at the same height for the field scenario. The map ignores
  altitude, but tables/debug/logs still expose `alt_mm`.

## Expected Map Behavior

- The map is the primary live field surface in the control app.
- The connected node is the `this`/`me` marker; browser/laptop geolocation is
  not used.
- GPS/GNSS positions appear as blue/green markers.
- Accepted radio-estimated positions appear as amber/orange markers with
  `(no GPS)` in the label.
- Rejected or no-solution nodes do not appear as live markers.
- Accepted degraded radio positions may appear, but with degraded styling.
- Recently stale positions remain briefly as faded markers with age available
  in details. Field evidence is fresh under 5 seconds, stale from 5 to 30
  seconds, and expired after 30 seconds.
- The map shows local-to-peer range links with distance and age. Peer-to-peer
  ranges remain visible in the distance table and NDJSON capture.
- The Field checklist keeps fixed rows for nodes 0 through 3 and highlights
  node ID presence, GNSS availability, local range readiness, anchor readiness,
  and current solution/source state.
- Anchor triangles and geometry overlays are out of scope.

## Pass Criteria

- Three GPS-enabled nodes appear as live GNSS markers around the test triangle.
- The GPS-disabled node appears as `node-N (no GPS)` only after it has an
  accepted `RADIO_3D` estimate.
- The GPS-disabled node marker is visually distinct from GNSS markers by color
  and label.
- Local-to-peer range links are visible for usable ranges and show their age.
- The Field checklist shows three non-local anchors as ready or stale-ok before
  the GPS-disabled node is expected to solve.
- The map auto-fits or follows visible nodes until the operator pans or zooms,
  then resumes only after recentering.
- Peer tables, serial logs, and debug details still expose altitude and
  diagnostic fields.
- The Record button downloads an NDJSON capture containing `meta`, `snapshot`,
  `range`, `node_quality`, and `log` records as they arrive.
- The app remains useful without internet access when `kyiv-oblast.pmtiles` and
  vendored assets are present.
- The implementation workflow downloads or builds the Kyiv offline PMTiles
  sidecar for field use, but normal automated tests do not require the full
  Kyiv bundle.
- The current field bundle was extracted from
  `https://build.protomaps.com/20260702.pmtiles` with bounding box
  `29.1,49.1,32.3,51.6`; the local output is about 214 MB and has SHA-256
  `d32c8915529ec4eea455929519f560db380c9472c83a2e250a733ce98a7af576`.

## Demo Fixture

Before real GPS firmware is available, test the map with a deterministic
four-node snapshot or fixture:

- three nodes with `position_source = GNSS` and `position_valid = true`
- one middle node with `position_source = RADIO_3D`,
  `position_valid = true`, and no usable GNSS
- no browser geolocation
- local-to-peer range links from the middle node to the three anchors

## Automated Offline Smoke Test

- Load the control app against the deterministic four-node fixture in an
  offline browser smoke test.
- Use only local vendored browser assets and local map/fallback data; do not
  depend on CDNs or online tiles.
- Verify the map renders nonblank.
- Verify the three GNSS markers use the GNSS style and labels.
- Verify the `RADIO_3D` marker uses the no-GPS estimated style and label.
- Verify altitude is absent from map labels but remains present in tables or
  debug details.
- Verify the Field checklist and local range links render from the deterministic
  fixture.
- Verify the browser can record typed and text telemetry into downloadable
  NDJSON offline.
- Capture a screenshot artifact for review when the smoke test runs locally or
  in CI.
