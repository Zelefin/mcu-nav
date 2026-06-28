#!/usr/bin/env python3
"""Compare a replay RADIO_3D solution with the reference trilateration solver."""

from __future__ import annotations

import argparse
import csv
import math
import os
from pathlib import Path
import sys


EARTH_RADIUS_M = 6378137.0


def rows_from_csv(path: Path):
    with path.open(newline="") as f:
        filtered = (line for line in f if line.strip() and not line.lstrip().startswith("#"))
        yield from csv.DictReader(filtered)


def import_reference_solver(trilat_root: Path):
    src = trilat_root / "src"
    if not src.exists():
        raise RuntimeError(f"missing reference source directory: {src}")
    sys.path.insert(0, str(src))
    try:
        from trilateration.solver import Anchor, solve_position
    except Exception as exc:  # noqa: BLE001 - report dependency/import failures clearly.
        venv_python = trilat_root / ".venv" / "bin" / "python"
        if (
            venv_python.exists()
            and os.environ.get("NAV_REPLAY_REF_REEXEC") != "1"
            and Path(sys.executable) != venv_python
        ):
            env = os.environ.copy()
            env["NAV_REPLAY_REF_REEXEC"] = "1"
            os.execve(str(venv_python), [str(venv_python), *sys.argv], env)
        raise RuntimeError(
            "failed to import trilateration reference solver; activate its venv "
            "or install dependencies with `pip install -e .[dev]`"
        ) from exc
    return Anchor, solve_position


def parse_replay_inputs(events_path: Path):
    altitude_m: float | None = None
    telemetry: dict[int, dict[str, str]] = {}
    ranges_m: dict[int, float] = {}

    for row in rows_from_csv(events_path):
        event_type = row.get("event_type", "")
        if event_type == "LOCAL_ALTITUDE_SAMPLE" and row.get("alt_valid") in {"1", "true", "TRUE"}:
            altitude_m = int(row["alt_mm"]) / 1000.0
        elif event_type == "PEER_BEACON_RX":
            peer_id = int(row["peer_id"])
            telemetry[peer_id] = row
        elif event_type == "RANGE_RESULT" and row.get("range_valid") in {"1", "true", "TRUE"}:
            peer_id = int(row["peer_id"])
            ranges_m[peer_id] = int(row["range_mm"]) / 1000.0

    if altitude_m is None:
        raise RuntimeError("events.csv does not contain a valid LOCAL_ALTITUDE_SAMPLE")

    anchor_inputs = []
    for peer_id in sorted(telemetry):
        if peer_id not in ranges_m:
            continue
        row = telemetry[peer_id]
        anchor_inputs.append(
            {
                "node_id": str(peer_id),
                "lat_deg": int(row["lat_e7"]) / 10000000.0,
                "lon_deg": int(row["lon_e7"]) / 10000000.0,
                "alt_m": int(row["alt_mm"]) / 1000.0,
                "distance_m": ranges_m[peer_id],
            }
        )

    if len(anchor_inputs) != 3:
        raise RuntimeError(f"expected exactly 3 complete anchors, got {len(anchor_inputs)}")
    return altitude_m, anchor_inputs


def final_radio_solution(solution_path: Path):
    final = None
    for row in rows_from_csv(solution_path):
        if row.get("solution_status") == "RADIO_3D" and row.get("solution_source") == "RADIO_3D":
            final = row
    if final is None:
        raise RuntimeError("solution.csv has no RADIO_3D solution from RADIO_3D source")
    return {
        "lat_deg": int(final["lat_e7"]) / 10000000.0,
        "lon_deg": int(final["lon_e7"]) / 10000000.0,
        "alt_m": int(final["alt_mm"]) / 1000.0,
    }


def horizontal_error_m(a_lat: float, a_lon: float, b_lat: float, b_lon: float) -> float:
    mean_lat = math.radians((a_lat + b_lat) * 0.5)
    dx = math.radians(a_lon - b_lon) * math.cos(mean_lat) * EARTH_RADIUS_M
    dy = math.radians(a_lat - b_lat) * EARTH_RADIUS_M
    return math.hypot(dx, dy)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--events", required=True, type=Path)
    parser.add_argument("--solution", required=True, type=Path)
    parser.add_argument("--trilat-root", default=Path("~/projects/trilateration"), type=Path)
    parser.add_argument("--max-horizontal-error-m", default=1.0, type=float)
    parser.add_argument("--max-vertical-error-m", default=1.0, type=float)
    args = parser.parse_args()

    trilat_root = args.trilat_root.expanduser().resolve()
    try:
        Anchor, solve_position = import_reference_solver(trilat_root)
        target_alt_m, anchor_inputs = parse_replay_inputs(args.events)
        anchors = [Anchor(**data) for data in anchor_inputs]
        reference = solve_position(anchors, target_alt_m)
        replay = final_radio_solution(args.solution)
    except Exception as exc:  # noqa: BLE001 - command line helper should print clear failures.
        print(f"REFERENCE_COMPARE_ERROR: {exc}", file=sys.stderr)
        return 2

    horizontal_m = horizontal_error_m(reference.lat_deg, reference.lon_deg, replay["lat_deg"], replay["lon_deg"])
    vertical_m = abs(reference.alt_m - replay["alt_m"])
    passed = horizontal_m <= args.max_horizontal_error_m and vertical_m <= args.max_vertical_error_m

    print("trilat_reference_compare:")
    print(f"  reference_lat_deg={reference.lat_deg:.9f}")
    print(f"  reference_lon_deg={reference.lon_deg:.9f}")
    print(f"  reference_alt_m={reference.alt_m:.3f}")
    print(f"  replay_lat_deg={replay['lat_deg']:.9f}")
    print(f"  replay_lon_deg={replay['lon_deg']:.9f}")
    print(f"  replay_alt_m={replay['alt_m']:.3f}")
    print(f"  horizontal_error_m={horizontal_m:.6f}")
    print(f"  vertical_error_m={vertical_m:.6f}")
    print(f"  result={'PASS' if passed else 'FAIL'}")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
