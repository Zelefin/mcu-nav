# Replay Tool

`nav_replay` reads deterministic `events.csv` files, feeds `nav_event_t` values
into the portable core, and emits:

- `solution.csv`
- `peers.csv`
- `logs.txt`

Example:

```bash
./build/tools/replay/nav_replay \
  --events examples/replay/radio_3d_success/events.csv \
  --out-dir build/replay/radio_3d_success \
  --node-id 0 \
  --pretty
```

See `docs/replay_csv.md` for the schema and limitations.
