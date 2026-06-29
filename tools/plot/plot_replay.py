#!/usr/bin/env python3
"""Generate replay diagnostic PNG plots from replay CSV outputs."""

from __future__ import annotations

import argparse
import csv
import json
import math
from pathlib import Path
import sys
from typing import Any

try:
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
except Exception as exc:  # noqa: BLE001 - CLI should report dependency failures clearly.
    print("PLOT_ERROR: failed to import matplotlib; install with `python -m pip install matplotlib`", file=sys.stderr)
    raise SystemExit(2) from exc


EARTH_RADIUS_M = 6378137.0


class PlotError(ValueError):
    pass


def require_file(path: Path, label: str) -> None:
    if not path.is_file():
        raise PlotError(f"missing {label}: {path}")


def read_csv(path: Path) -> list[dict[str, str]]:
    require_file(path, path.name)
    with path.open(newline="") as f:
        filtered = (line for line in f if line.strip() and not line.lstrip().startswith("#"))
        return list(csv.DictReader(filtered))


def to_int(row: dict[str, str], key: str, default: int = 0) -> int:
    text = row.get(key, "")
    return int(text) if text else default


def to_float(row: dict[str, str], key: str, default: float = 0.0) -> float:
    text = row.get(key, "")
    return float(text) if text else default


def xy_m(lat_e7: int, lon_e7: int, ref_lat_e7: int, ref_lon_e7: int) -> tuple[float, float]:
    lat = math.radians(lat_e7 / 1e7)
    lon = math.radians(lon_e7 / 1e7)
    ref_lat = math.radians(ref_lat_e7 / 1e7)
    ref_lon = math.radians(ref_lon_e7 / 1e7)
    x = (lon - ref_lon) * math.cos(ref_lat) * EARTH_RADIUS_M
    y = (lat - ref_lat) * EARTH_RADIUS_M
    return x, y


def truth_by_time(rows: list[dict[str, str]]) -> dict[int, dict[str, str]]:
    return {to_int(row, "time_ms"): row for row in rows}


def radio_solution_rows(rows: list[dict[str, str]]) -> list[dict[str, str]]:
    return [row for row in rows if row.get("solution_status") == "RADIO_3D" and row.get("solution_source") == "RADIO_3D"]


def save_or_empty(path: Path, title: str, message: str) -> None:
    plt.figure(figsize=(8, 4.5))
    plt.title(title)
    plt.text(0.5, 0.5, message, ha="center", va="center", transform=plt.gca().transAxes)
    plt.axis("off")
    plt.tight_layout()
    plt.savefig(path, dpi=140)
    plt.close()


def plot_trajectory(path: Path, truth: list[dict[str, str]], solutions: list[dict[str, str]], peers: list[dict[str, str]]) -> None:
    ref = truth[0]
    ref_lat = to_int(ref, "true_lat_e7")
    ref_lon = to_int(ref, "true_lon_e7")
    truth_xy = [xy_m(to_int(row, "true_lat_e7"), to_int(row, "true_lon_e7"), ref_lat, ref_lon) for row in truth]
    sol_xy = [xy_m(to_int(row, "lat_e7"), to_int(row, "lon_e7"), ref_lat, ref_lon) for row in solutions]
    latest_peer: dict[int, dict[str, str]] = {}
    for row in peers:
        latest_peer[to_int(row, "peer_id")] = row

    plt.figure(figsize=(7, 7))
    plt.plot([p[0] for p in truth_xy], [p[1] for p in truth_xy], "-o", label="truth")
    if sol_xy:
        plt.plot([p[0] for p in sol_xy], [p[1] for p in sol_xy], ".-", label="RADIO_3D estimate")
    if latest_peer:
        anchor_xy = [xy_m(to_int(row, "lat_e7"), to_int(row, "lon_e7"), ref_lat, ref_lon) for row in latest_peer.values()]
        plt.scatter([p[0] for p in anchor_xy], [p[1] for p in anchor_xy], marker="^", label="anchors")
        for peer_id, row in latest_peer.items():
            x, y = xy_m(to_int(row, "lat_e7"), to_int(row, "lon_e7"), ref_lat, ref_lon)
            plt.annotate(str(peer_id), (x, y), textcoords="offset points", xytext=(4, 4))
    plt.title("Replay Trajectory XY")
    plt.xlabel("East from first truth point (m)")
    plt.ylabel("North from first truth point (m)")
    plt.axis("equal")
    plt.grid(True, alpha=0.3)
    plt.legend()
    plt.tight_layout()
    plt.savefig(path, dpi=140)
    plt.close()


def error_series(solutions: list[dict[str, str]], truth_map: dict[int, dict[str, str]]) -> tuple[list[int], list[float], list[float]]:
    times: list[int] = []
    horizontal: list[float] = []
    vertical: list[float] = []
    for row in solutions:
        time_ms = to_int(row, "time_ms")
        truth = truth_map.get(time_ms)
        if truth is None:
            continue
        sx, sy = xy_m(to_int(row, "lat_e7"), to_int(row, "lon_e7"), to_int(truth, "true_lat_e7"), to_int(truth, "true_lon_e7"))
        times.append(time_ms)
        horizontal.append(math.hypot(sx, sy))
        vertical.append((to_int(row, "alt_mm") - to_int(truth, "true_alt_mm")) / 1000.0)
    return times, horizontal, vertical


def plot_line(path: Path, title: str, xlabel: str, ylabel: str, series: list[tuple[list[int], list[float], str]]) -> None:
    plt.figure(figsize=(8, 4.5))
    plotted = False
    for xs, ys, label in series:
        if xs and ys:
            plt.plot(xs, ys, ".-", label=label)
            plotted = True
    plt.title(title)
    plt.xlabel(xlabel)
    plt.ylabel(ylabel)
    plt.grid(True, alpha=0.3)
    if plotted:
        plt.legend()
    plt.tight_layout()
    plt.savefig(path, dpi=140)
    plt.close()


def plot_residuals(path: Path, solutions: list[dict[str, str]]) -> None:
    times = [to_int(row, "time_ms") for row in solutions]
    series = []
    for index in range(3):
        series.append((times, [to_int(row, f"residual{index}_mm") / 1000.0 for row in solutions], f"residual{index}"))
    plot_line(path, "Anchor Residuals", "time (ms)", "residual (m)", series)


def plot_quality(path: Path, solutions: list[dict[str, str]]) -> None:
    times = [to_int(row, "time_ms") for row in solutions]
    plot_line(
        path,
        "Solution Quality",
        "time (ms)",
        "value",
        [
            (times, [to_float(row, "total_quality") for row in solutions], "total_quality"),
            (times, [to_float(row, "residual_rms_m") for row in solutions], "residual_rms_m"),
        ],
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--truth", required=True, type=Path)
    parser.add_argument("--solution", required=True, type=Path)
    parser.add_argument("--peers", required=True, type=Path)
    parser.add_argument("--compare-report", required=True, type=Path)
    parser.add_argument("--out-dir", required=True, type=Path)
    parser.add_argument("--pretty", action="store_true")
    args = parser.parse_args()

    try:
        truth = read_csv(args.truth)
        solution = read_csv(args.solution)
        peers = read_csv(args.peers)
        require_file(args.compare_report, "compare report")
        with args.compare_report.open() as f:
            json.load(f)
        if not truth:
            raise PlotError(f"truth has no rows: {args.truth}")
        args.out_dir.mkdir(parents=True, exist_ok=True)
        radio = radio_solution_rows(solution)
        if not radio:
            save_or_empty(args.out_dir / "trajectory_xy.png", "Replay Trajectory XY", "No RADIO_3D solution rows")
            save_or_empty(args.out_dir / "horizontal_error.png", "Horizontal Error", "No RADIO_3D solution rows")
            save_or_empty(args.out_dir / "altitude_error.png", "Altitude Error", "No RADIO_3D solution rows")
            save_or_empty(args.out_dir / "residuals.png", "Anchor Residuals", "No RADIO_3D solution rows")
            save_or_empty(args.out_dir / "solution_quality.png", "Solution Quality", "No RADIO_3D solution rows")
        else:
            plot_trajectory(args.out_dir / "trajectory_xy.png", truth, radio, peers)
            times, horizontal, vertical = error_series(radio, truth_by_time(truth))
            plot_line(args.out_dir / "horizontal_error.png", "Horizontal Error", "time (ms)", "error (m)", [(times, horizontal, "horizontal")])
            plot_line(args.out_dir / "altitude_error.png", "Altitude Error", "time (ms)", "estimate - truth (m)", [(times, vertical, "vertical")])
            plot_residuals(args.out_dir / "residuals.png", radio)
            plot_quality(args.out_dir / "solution_quality.png", radio)
    except (OSError, ValueError, PlotError) as exc:
        print(f"PLOT_ERROR: {exc}", file=sys.stderr)
        return 2

    if args.pretty:
        print("plots generated:")
        for name in ["trajectory_xy.png", "horizontal_error.png", "altitude_error.png", "residuals.png", "solution_quality.png"]:
            print(f"  {args.out_dir / name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
