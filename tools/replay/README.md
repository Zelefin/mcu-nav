# Replay Tool

`nav_replay` reads deterministic `events.csv` files, feeds `nav_event_t` values
into the portable core, and emits:

- `solution.csv`
- `peers.csv`
- `logs.txt`
- `compare_report.txt` and `compare_report.json` when `truth.csv` is available

Example:

```bash
./build/tools/replay/nav_replay \
  --events examples/replay/radio_3d_success/events.csv \
  --truth examples/replay/radio_3d_success/truth.csv \
  --config examples/replay/radio_3d_success/replay_config.csv \
  --out-dir build/replay/radio_3d_success \
  --node-id 0 \
  --pretty
```

`replay_config.csv` and `truth.csv` are auto-discovered beside `events.csv`
unless `--no-config` or `--no-truth` is used.

Optional Python reference comparison:

```bash
python tools/replay/compare_with_trilat_reference.py \
  --events examples/replay/radio_3d_success/events.csv \
  --solution build/replay/radio_3d_success/solution.csv \
  --trilat-root ~/projects/trilateration
```

The helper uses the reference project `.venv` automatically when available.

See `docs/replay_csv.md` for the schema and limitations.
