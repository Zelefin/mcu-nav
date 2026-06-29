#!/usr/bin/env python3
"""Generate deterministic replay CSV inputs from a software scenario JSON file."""

from __future__ import annotations

import argparse
import csv
import json
import math
from pathlib import Path
import random
import shutil
import subprocess
import sys
from typing import Any


EARTH_RADIUS_M = 6378137.0
WGS84_A_M = 6378137.0
WGS84_E2 = 6.69437999014e-3
EVENTS_HEADER = [
    "time_ms",
    "event_type",
    "node_id",
    "peer_id",
    "packet_seq",
    "request_id",
    "lat_e7",
    "lon_e7",
    "alt_mm",
    "vel_n_mmps",
    "vel_e_mmps",
    "vel_d_mmps",
    "fix_type",
    "gnss_valid",
    "satellites",
    "hdop_centi",
    "hacc_mm",
    "vacc_mm",
    "nav_mode",
    "range_mm",
    "range_sigma_mm",
    "rssi_dbm",
    "snr_db",
    "range_valid",
    "alt_source",
    "alt_valid",
    "range_fail_reason",
]
TRUTH_HEADER = [
    "time_ms",
    "node_id",
    "true_lat_e7",
    "true_lon_e7",
    "true_alt_mm",
    "true_vn_mmps",
    "true_ve_mmps",
    "true_vd_mmps",
]
CONFIG_KEYS = [
    "node_id",
    "telemetry_ttl_ms",
    "range_ttl_ms",
    "local_altitude_ttl_ms",
    "tick_period_ms",
    "max_range_sigma_mm",
    "min_anchor_quality",
    "min_solution_quality",
    "max_residual_rms_m",
    "max_residual_m",
    "min_anchor_triangle_area_m2",
    "degraded_anchor_triangle_area_m2",
    "demo_force_gps_denied",
    "allow_gnss_altitude_in_demo_forced_denied",
    "max_allowed_horizontal_error_m",
    "max_allowed_vertical_error_m",
    "max_allowed_3d_error_m",
    "expect_final_mode",
    "expect_final_solution",
    "expect_final_source",
    "expect_final_reject",
]
NAV_MODES = {
    "BOOT",
    "GNSS_ACQUIRE",
    "GNSS_OK",
    "GNSS_SUSPECT",
    "GPS_DENIED",
    "RADIO_NAV_OK",
    "RADIO_NAV_DEGRADED",
    "NO_NAV_SOLUTION",
    "DEMO_FORCED_DENIED",
}


class ScenarioError(ValueError):
    pass


def fail(path: str, message: str) -> None:
    raise ScenarioError(f"{path}: {message}")


def require_mapping(value: Any, path: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        fail(path, "expected object")
    return value


def require_list(value: Any, path: str) -> list[Any]:
    if not isinstance(value, list):
        fail(path, "expected array")
    return value


def require_int(obj: dict[str, Any], key: str, path: str, minimum: int | None = None, maximum: int | None = None) -> int:
    if key not in obj:
        fail(f"{path}.{key}", "missing required field")
    value = obj[key]
    if not isinstance(value, int) or isinstance(value, bool):
        fail(f"{path}.{key}", "expected integer")
    if minimum is not None and value < minimum:
        fail(f"{path}.{key}", f"expected >= {minimum}")
    if maximum is not None and value > maximum:
        fail(f"{path}.{key}", f"expected <= {maximum}")
    return value


def optional_number(obj: dict[str, Any], key: str, path: str, default: float) -> float:
    value = obj.get(key, default)
    if not isinstance(value, (int, float)) or isinstance(value, bool) or not math.isfinite(float(value)):
        fail(f"{path}.{key}", "expected finite number")
    return float(value)


def optional_bool(obj: dict[str, Any], key: str, path: str, default: bool) -> bool:
    value = obj.get(key, default)
    if not isinstance(value, bool):
        fail(f"{path}.{key}", "expected boolean")
    return value


def optional_string(obj: dict[str, Any], key: str, path: str, default: str) -> str:
    value = obj.get(key, default)
    if not isinstance(value, str) or not value:
        fail(f"{path}.{key}", "expected non-empty string")
    return value


def load_json(path: Path) -> dict[str, Any]:
    try:
        with path.open() as f:
            data = json.load(f)
    except json.JSONDecodeError as exc:
        raise ScenarioError(f"{path}:{exc.lineno}:{exc.colno}: invalid JSON: {exc.msg}") from exc
    except OSError as exc:
        raise ScenarioError(f"{path}: failed to read: {exc}") from exc
    return require_mapping(data, str(path))


def latlon_to_local(lat_e7: int, lon_e7: int, alt_mm: int, origin: dict[str, int]) -> tuple[float, float, float]:
    lat0 = math.radians(origin["lat_e7"] / 1e7)
    lat = math.radians(lat_e7 / 1e7)
    lon = math.radians(lon_e7 / 1e7)
    lon0 = math.radians(origin["lon_e7"] / 1e7)
    north_m = (lat - lat0) * EARTH_RADIUS_M
    east_m = (lon - lon0) * math.cos(lat0) * EARTH_RADIUS_M
    up_m = (alt_mm - origin["alt_mm"]) / 1000.0
    return north_m, east_m, up_m


def local_to_geodetic(north_m: float, east_m: float, up_m: float, origin: dict[str, int]) -> tuple[int, int, int]:
    lat0 = math.radians(origin["lat_e7"] / 1e7)
    lon0 = math.radians(origin["lon_e7"] / 1e7)
    lat = lat0 + north_m / EARTH_RADIUS_M
    lon = lon0 + east_m / (EARTH_RADIUS_M * math.cos(lat0))
    alt_mm = origin["alt_mm"] + int(round(up_m * 1000.0))
    return int(round(math.degrees(lat) * 1e7)), int(round(math.degrees(lon) * 1e7)), alt_mm


def geodetic_to_ecef_m(lat_e7: int, lon_e7: int, alt_mm: int) -> tuple[float, float, float]:
    lat = math.radians(lat_e7 / 1e7)
    lon = math.radians(lon_e7 / 1e7)
    alt_m = alt_mm / 1000.0
    sin_lat = math.sin(lat)
    cos_lat = math.cos(lat)
    n = WGS84_A_M / math.sqrt(1.0 - WGS84_E2 * sin_lat * sin_lat)
    x = (n + alt_m) * cos_lat * math.cos(lon)
    y = (n + alt_m) * cos_lat * math.sin(lon)
    z = (n * (1.0 - WGS84_E2) + alt_m) * sin_lat
    return x, y, z


def normalize_node(raw: Any, index: int, origin: dict[str, int]) -> dict[str, Any]:
    path = f"nodes[{index}]"
    node = require_mapping(raw, path)
    node_id = require_int(node, "node_id", path, 0, 3)
    role = optional_string(node, "role", path, "")
    if role not in {"anchor", "blind"}:
        fail(f"{path}.role", "expected anchor or blind")
    initial = require_mapping(node.get("initial_position"), f"{path}.initial_position")
    lat_e7 = require_int(initial, "lat_e7", f"{path}.initial_position", -900000000, 900000000)
    lon_e7 = require_int(initial, "lon_e7", f"{path}.initial_position", -1800000000, 1800000000)
    alt_mm = require_int(initial, "alt_mm", f"{path}.initial_position")
    velocity = require_mapping(node.get("velocity", {}), f"{path}.velocity")
    nav_mode = optional_string(node, "nav_mode", path, "GNSS_OK" if role == "anchor" else "DEMO_FORCED_DENIED")
    if nav_mode not in NAV_MODES:
        fail(f"{path}.nav_mode", f"unknown nav mode {nav_mode!r}")
    north_m, east_m, up_m = latlon_to_local(lat_e7, lon_e7, alt_mm, origin)
    return {
        "node_id": node_id,
        "role": role,
        "initial_position": {"lat_e7": lat_e7, "lon_e7": lon_e7, "alt_mm": alt_mm},
        "initial_local_m": {"north_m": north_m, "east_m": east_m, "up_m": up_m},
        "velocity": {
            "north_mmps": require_int(velocity, "north_mmps", f"{path}.velocity") if "north_mmps" in velocity else 0,
            "east_mmps": require_int(velocity, "east_mmps", f"{path}.velocity") if "east_mmps" in velocity else 0,
            "down_mmps": require_int(velocity, "down_mmps", f"{path}.velocity") if "down_mmps" in velocity else 0,
        },
        "gnss_valid": optional_bool(node, "gnss_valid", path, role == "anchor"),
        "nav_mode": nav_mode,
        "emit_local_gnss": optional_bool(node, "emit_local_gnss", path, False),
        "fix_type": optional_string(node, "fix_type", path, "3D"),
        "satellites": int(optional_number(node, "satellites", path, 12.0)),
        "hdop_centi": int(optional_number(node, "hdop_centi", path, 80.0)),
        "hacc_mm": int(optional_number(node, "hacc_mm", path, 1000.0)),
        "vacc_mm": int(optional_number(node, "vacc_mm", path, 1500.0)),
    }


def normalize_scenario(raw: dict[str, Any]) -> dict[str, Any]:
    scenario_name = optional_string(raw, "scenario_name", "scenario", "")
    local_node_id = require_int(raw, "local_node_id", "scenario", 0, 3)
    start_time_ms = require_int(raw, "start_time_ms", "scenario", 0)
    end_time_ms = require_int(raw, "end_time_ms", "scenario", 0)
    step_ms = require_int(raw, "step_ms", "scenario", 1)
    if end_time_ms < start_time_ms:
        fail("scenario.end_time_ms", "must be >= start_time_ms")
    origin = {
        "lat_e7": require_int(raw, "origin_lat_e7", "scenario", -900000000, 900000000),
        "lon_e7": require_int(raw, "origin_lon_e7", "scenario", -1800000000, 1800000000),
        "alt_mm": require_int(raw, "origin_alt_mm", "scenario"),
    }
    nodes = [normalize_node(item, i, origin) for i, item in enumerate(require_list(raw.get("nodes"), "scenario.nodes"))]
    node_ids = [node["node_id"] for node in nodes]
    if len(set(node_ids)) != len(node_ids):
        fail("scenario.nodes", "node_id values must be unique")
    if local_node_id not in node_ids:
        fail("scenario.local_node_id", "must match one node_id")
    blind_nodes = [node for node in nodes if node["role"] == "blind"]
    if len(blind_nodes) != 1:
        fail("scenario.nodes", "exactly one blind node is required")
    if blind_nodes[0]["node_id"] != local_node_id:
        fail("scenario.nodes", "the blind node must be the local_node_id")
    anchors = [node for node in nodes if node["role"] == "anchor"]
    if len(anchors) < 1:
        fail("scenario.nodes", "at least one anchor is required")

    range_model = require_mapping(raw.get("range_model", {}), "scenario.range_model")
    noise_std_m = optional_number(range_model, "noise_std_m", "scenario.range_model", 0.0)
    if noise_std_m < 0.0:
        fail("scenario.range_model.noise_std_m", "expected >= 0")
    bias_raw = require_mapping(range_model.get("bias_by_peer_m", {}), "scenario.range_model.bias_by_peer_m")
    bias_by_peer_m: dict[str, float] = {}
    for peer, value in bias_raw.items():
        if not str(peer).isdigit():
            fail("scenario.range_model.bias_by_peer_m", "peer keys must be numeric strings")
        if not isinstance(value, (int, float)) or isinstance(value, bool):
            fail(f"scenario.range_model.bias_by_peer_m.{peer}", "expected number")
        bias_by_peer_m[str(peer)] = float(value)
    seed = range_model.get("seed", 0)
    if not isinstance(seed, int) or isinstance(seed, bool):
        fail("scenario.range_model.seed", "expected integer")
    range_sigma_mm = int(optional_number(range_model, "range_sigma_mm", "scenario.range_model", 100.0))
    if range_sigma_mm < 0:
        fail("scenario.range_model.range_sigma_mm", "expected >= 0")

    packet_loss = require_mapping(raw.get("packet_loss", {"mode": "none"}), "scenario.packet_loss")
    packet_loss_mode = optional_string(packet_loss, "mode", "scenario.packet_loss", "none")
    if packet_loss_mode not in {"none", "drop_every_n", "explicit"}:
        fail("scenario.packet_loss.mode", "expected none, drop_every_n, or explicit")
    explicit_drops = []
    for i, drop_raw in enumerate(require_list(packet_loss.get("explicit_drops", []), "scenario.packet_loss.explicit_drops")):
        drop = require_mapping(drop_raw, f"scenario.packet_loss.explicit_drops[{i}]")
        event_type = optional_string(drop, "event_type", f"scenario.packet_loss.explicit_drops[{i}]", "RANGE_RESULT")
        if event_type not in {"RANGE_RESULT", "PEER_BEACON_RX"}:
            fail(f"scenario.packet_loss.explicit_drops[{i}].event_type", "expected RANGE_RESULT or PEER_BEACON_RX")
        explicit_drops.append(
            {
                "time_ms": require_int(drop, "time_ms", f"scenario.packet_loss.explicit_drops[{i}]", 0),
                "peer_id": require_int(drop, "peer_id", f"scenario.packet_loss.explicit_drops[{i}]", 0, 3),
                "event_type": event_type,
            }
        )

    replay_config = require_mapping(raw.get("replay_config", {}), "scenario.replay_config")
    unknown_config = sorted(set(replay_config) - set(CONFIG_KEYS))
    if unknown_config:
        fail("scenario.replay_config", f"unknown replay config key {unknown_config[0]!r}")
    replay_config = {
        "node_id": local_node_id,
        "telemetry_ttl_ms": 1500,
        "range_ttl_ms": 1500,
        "local_altitude_ttl_ms": 1500,
        "tick_period_ms": step_ms,
        "max_range_sigma_mm": 5000,
        "min_anchor_quality": 0.1,
        "min_solution_quality": 0.1,
        "max_residual_rms_m": 10.0,
        "max_residual_m": 10.0,
        "min_anchor_triangle_area_m2": 10.0,
        "degraded_anchor_triangle_area_m2": 100.0,
        "demo_force_gps_denied": True,
        "allow_gnss_altitude_in_demo_forced_denied": False,
        "max_allowed_horizontal_error_m": 1.0,
        "max_allowed_vertical_error_m": 1.0,
        "max_allowed_3d_error_m": 1.0,
        **replay_config,
    }
    replay_config["node_id"] = local_node_id

    return {
        "scenario_name": scenario_name,
        "local_node_id": local_node_id,
        "start_time_ms": start_time_ms,
        "end_time_ms": end_time_ms,
        "step_ms": step_ms,
        "origin_lat_e7": origin["lat_e7"],
        "origin_lon_e7": origin["lon_e7"],
        "origin_alt_mm": origin["alt_mm"],
        "nodes": nodes,
        "range_model": {
            "enabled": optional_bool(range_model, "enabled", "scenario.range_model", True),
            "noise_std_m": noise_std_m,
            "bias_by_peer_m": bias_by_peer_m,
            "seed": seed,
            "range_sigma_mm": range_sigma_mm,
        },
        "packet_loss": {
            "mode": packet_loss_mode,
            "drop_every_n": int(optional_number(packet_loss, "drop_every_n", "scenario.packet_loss", 0.0)),
            "explicit_drops": explicit_drops,
        },
        "replay_config": replay_config,
        "output_dir": raw.get("output_dir", ""),
    }


def position_at(node: dict[str, Any], scenario: dict[str, Any], time_ms: int) -> dict[str, Any]:
    elapsed_s = (time_ms - scenario["start_time_ms"]) / 1000.0
    initial = node["initial_local_m"]
    velocity = node["velocity"]
    north_m = initial["north_m"] + velocity["north_mmps"] * elapsed_s / 1000.0
    east_m = initial["east_m"] + velocity["east_mmps"] * elapsed_s / 1000.0
    up_m = initial["up_m"] - velocity["down_mmps"] * elapsed_s / 1000.0
    origin = {
        "lat_e7": scenario["origin_lat_e7"],
        "lon_e7": scenario["origin_lon_e7"],
        "alt_mm": scenario["origin_alt_mm"],
    }
    lat_e7, lon_e7, alt_mm = local_to_geodetic(north_m, east_m, up_m, origin)
    return {
        "north_m": north_m,
        "east_m": east_m,
        "up_m": up_m,
        "lat_e7": lat_e7,
        "lon_e7": lon_e7,
        "alt_mm": alt_mm,
    }


def blank_event(time_ms: int, event_type: str) -> dict[str, Any]:
    row = {key: "" for key in EVENTS_HEADER}
    row["time_ms"] = time_ms
    row["event_type"] = event_type
    return row


def should_drop(scenario: dict[str, Any], time_ms: int, peer_id: int, event_type: str, range_counter: int) -> bool:
    packet_loss = scenario["packet_loss"]
    if packet_loss["mode"] == "none":
        return False
    if packet_loss["mode"] == "drop_every_n" and event_type == "RANGE_RESULT":
        n = packet_loss["drop_every_n"]
        return n > 0 and range_counter % n == 0
    for drop in packet_loss["explicit_drops"]:
        if drop["time_ms"] == time_ms and drop["peer_id"] == peer_id and drop["event_type"] == event_type:
            return True
    return False


def generate_rows(scenario: dict[str, Any]) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    events: list[dict[str, Any]] = []
    truth: list[dict[str, Any]] = []
    rng = random.Random(scenario["range_model"]["seed"])
    local = next(node for node in scenario["nodes"] if node["node_id"] == scenario["local_node_id"])
    anchors = sorted((node for node in scenario["nodes"] if node["role"] == "anchor"), key=lambda item: item["node_id"])
    range_counter = 0

    step_index = 0
    time_ms = scenario["start_time_ms"]
    while time_ms <= scenario["end_time_ms"]:
        local_pos = position_at(local, scenario, time_ms)
        truth.append(
            {
                "time_ms": time_ms,
                "node_id": local["node_id"],
                "true_lat_e7": local_pos["lat_e7"],
                "true_lon_e7": local_pos["lon_e7"],
                "true_alt_mm": local_pos["alt_mm"],
                "true_vn_mmps": local["velocity"]["north_mmps"],
                "true_ve_mmps": local["velocity"]["east_mmps"],
                "true_vd_mmps": local["velocity"]["down_mmps"],
            }
        )

        events.append(blank_event(time_ms, "TICK"))
        alt_row = blank_event(time_ms, "LOCAL_ALTITUDE_SAMPLE")
        alt_row.update({"alt_mm": local_pos["alt_mm"], "alt_source": "SIM", "alt_valid": 1})
        events.append(alt_row)

        if local["emit_local_gnss"]:
            row = blank_event(time_ms, "LOCAL_GNSS_SAMPLE")
            row.update(
                {
                    "node_id": local["node_id"],
                    "lat_e7": local_pos["lat_e7"],
                    "lon_e7": local_pos["lon_e7"],
                    "alt_mm": local_pos["alt_mm"],
                    "vel_n_mmps": local["velocity"]["north_mmps"],
                    "vel_e_mmps": local["velocity"]["east_mmps"],
                    "vel_d_mmps": local["velocity"]["down_mmps"],
                    "fix_type": local["fix_type"],
                    "gnss_valid": 1 if local["gnss_valid"] else 0,
                    "satellites": local["satellites"],
                    "hdop_centi": local["hdop_centi"],
                    "hacc_mm": local["hacc_mm"],
                    "vacc_mm": local["vacc_mm"],
                }
            )
            events.append(row)

        for anchor in anchors:
            peer_id = anchor["node_id"]
            anchor_pos = position_at(anchor, scenario, time_ms)
            if not should_drop(scenario, time_ms, peer_id, "PEER_BEACON_RX", range_counter):
                row = blank_event(time_ms, "PEER_BEACON_RX")
                row.update(
                    {
                        "peer_id": peer_id,
                        "packet_seq": step_index * 100 + peer_id,
                        "lat_e7": anchor_pos["lat_e7"],
                        "lon_e7": anchor_pos["lon_e7"],
                        "alt_mm": anchor_pos["alt_mm"],
                        "vel_n_mmps": anchor["velocity"]["north_mmps"],
                        "vel_e_mmps": anchor["velocity"]["east_mmps"],
                        "vel_d_mmps": anchor["velocity"]["down_mmps"],
                        "fix_type": anchor["fix_type"],
                        "gnss_valid": 1 if anchor["gnss_valid"] else 0,
                        "satellites": anchor["satellites"],
                        "hdop_centi": anchor["hdop_centi"],
                        "hacc_mm": anchor["hacc_mm"],
                        "vacc_mm": anchor["vacc_mm"],
                        "nav_mode": anchor["nav_mode"],
                        "rssi_dbm": -60,
                        "snr_db": 10,
                    }
                )
                events.append(row)

            range_counter += 1
            if not scenario["range_model"]["enabled"]:
                continue
            if should_drop(scenario, time_ms, peer_id, "RANGE_RESULT", range_counter):
                continue
            local_ecef = geodetic_to_ecef_m(local_pos["lat_e7"], local_pos["lon_e7"], local_pos["alt_mm"])
            anchor_ecef = geodetic_to_ecef_m(anchor_pos["lat_e7"], anchor_pos["lon_e7"], anchor_pos["alt_mm"])
            dx = anchor_ecef[0] - local_ecef[0]
            dy = anchor_ecef[1] - local_ecef[1]
            dz = anchor_ecef[2] - local_ecef[2]
            range_m = math.sqrt(dx * dx + dy * dy + dz * dz)
            range_m += scenario["range_model"]["bias_by_peer_m"].get(str(peer_id), 0.0)
            if scenario["range_model"]["noise_std_m"] > 0.0:
                range_m += rng.gauss(0.0, scenario["range_model"]["noise_std_m"])
            row = blank_event(time_ms, "RANGE_RESULT")
            row.update(
                {
                    "peer_id": peer_id,
                    "request_id": step_index * 100 + 50 + peer_id,
                    "range_mm": max(0, int(round(range_m * 1000.0))),
                    "range_sigma_mm": scenario["range_model"]["range_sigma_mm"],
                    "rssi_dbm": -60,
                    "snr_db": 10,
                    "range_valid": 1,
                }
            )
            events.append(row)

        events.append(blank_event(time_ms, "TICK"))
        step_index += 1
        time_ms += scenario["step_ms"]

    return events, truth


def write_events(path: Path, rows: list[dict[str, Any]]) -> None:
    with path.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=EVENTS_HEADER, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def write_truth(path: Path, rows: list[dict[str, Any]]) -> None:
    with path.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=TRUTH_HEADER, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def config_value(value: Any) -> str:
    if isinstance(value, bool):
        return "true" if value else "false"
    return str(value)


def write_config(path: Path, replay_config: dict[str, Any]) -> None:
    with path.open("w", newline="") as f:
        writer = csv.writer(f, lineterminator="\n")
        writer.writerow(["key", "value"])
        for key in CONFIG_KEYS:
            if key in replay_config:
                writer.writerow([key, config_value(replay_config[key])])


def prepare_output_dir(path: Path, overwrite: bool) -> None:
    if path.exists():
        if not overwrite:
            raise ScenarioError(f"{path}: output directory exists; pass --overwrite to replace it")
        shutil.rmtree(path)
    path.mkdir(parents=True)


def run_replay(replay_exe: Path, out_dir: Path, pretty: bool) -> None:
    replay_dir = out_dir / "replay"
    cmd = [
        str(replay_exe),
        "--events",
        str(out_dir / "events.csv"),
        "--truth",
        str(out_dir / "truth.csv"),
        "--config",
        str(out_dir / "replay_config.csv"),
        "--out-dir",
        str(replay_dir),
    ]
    if pretty:
        cmd.append("--pretty")
    completed = subprocess.run(cmd, text=True)
    if completed.returncode != 0:
        raise ScenarioError(f"nav_replay failed with exit code {completed.returncode}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scenario", required=True, type=Path)
    parser.add_argument("--out-dir", required=True, type=Path)
    parser.add_argument("--overwrite", action="store_true")
    parser.add_argument("--pretty", action="store_true")
    parser.add_argument("--run-replay", type=Path)
    args = parser.parse_args()

    try:
        raw = load_json(args.scenario)
        scenario = normalize_scenario(raw)
        scenario["output_dir"] = str(args.out_dir)
        prepare_output_dir(args.out_dir, args.overwrite)
        events, truth = generate_rows(scenario)
        write_events(args.out_dir / "events.csv", events)
        write_truth(args.out_dir / "truth.csv", truth)
        write_config(args.out_dir / "replay_config.csv", scenario["replay_config"])
        with (args.out_dir / "scenario_resolved.json").open("w") as f:
            json.dump(scenario, f, indent=2, sort_keys=True)
            f.write("\n")
        if args.run_replay is not None:
            run_replay(args.run_replay, args.out_dir, args.pretty)
    except ScenarioError as exc:
        print(f"SCENARIO_ERROR: {exc}", file=sys.stderr)
        return 2

    if args.pretty:
        print("scenario generated:")
        print(f"  events_csv={args.out_dir / 'events.csv'}")
        print(f"  truth_csv={args.out_dir / 'truth.csv'}")
        print(f"  replay_config_csv={args.out_dir / 'replay_config.csv'}")
        print(f"  scenario_resolved_json={args.out_dir / 'scenario_resolved.json'}")
        if args.run_replay is not None:
            print(f"  replay_dir={args.out_dir / 'replay'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
