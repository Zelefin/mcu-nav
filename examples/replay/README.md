# Replay Fixtures

Each fixture is deterministic. `events.csv` is the replay input.
`replay_config.csv` contains expected final status checks and fixture-specific
thresholds. `truth.csv`, when present, is used only for comparison; replay never
generates motion or random measurements.

Success fixture:

- `radio_3d_success`: final `RADIO_NAV_OK`, `RADIO_3D`, source `RADIO_3D`,
  reject `NONE`; includes `truth.csv` and comparison thresholds.

Rejection fixtures:

- `reject_not_enough_anchors`: final aggregate reject `NOT_ENOUGH_ANCHORS`.
- `reject_missing_altitude`: final reject `MISSING_LOCAL_ALTITUDE`.
- `reject_stale_range`: final aggregate reject `NOT_ENOUGH_ANCHORS`; root cause
  `STALE_RANGE` is visible in `peers.csv` and `logs.txt`.
- `reject_bad_position`: final aggregate reject `NOT_ENOUGH_ANCHORS`; root
  cause `BAD_POSITION` is visible in `peers.csv` and `logs.txt`.

Parser-negative fixtures under `parser_invalid_*` are intended to fail and keep
line-numbered error handling covered by CTest.
