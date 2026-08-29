#!/usr/bin/env python3
"""Summarize the local, opt-in PocketOS health-monitor CSV from an SD card."""

from __future__ import annotations

import csv
import sys
from datetime import datetime
from pathlib import Path


def number(row: dict[str, str], key: str) -> int | None:
    try:
        value = int(row[key])
    except (KeyError, TypeError, ValueError):
        return None
    return value if value >= 0 else None


def span(values: list[int], unit: str) -> str:
    if not values:
        return "not available"
    return f"{min(values):,}–{max(values):,} {unit}"


def report(path: Path, title: str, rss_key: str) -> tuple[int | None, int]:
    if not path.is_file():
        print("No health log found.")
        print(f"Expected: {path}")
        return None, 1

    with path.open(newline="", encoding="utf-8") as handle:
        rows = list(csv.DictReader(handle))
    if not rows:
        print("The health log is enabled but has no samples yet. Use the launcher for a few minutes, then try again.")
        return None, 0

    timestamps = [number(row, "timestamp") for row in rows]
    timestamps = [value for value in timestamps if value is not None]
    rss = [number(row, rss_key) for row in rows]
    memory = [number(row, "mem_available_kb") for row in rows]
    battery = [number(row, "battery_percent") for row in rows]
    rss = [value for value in rss if value is not None]
    memory = [value for value in memory if value is not None]
    battery = [value for value in battery if value is not None]

    print(title)
    print("=" * 22)
    print(f"Samples: {len(rows)}")
    if timestamps:
        start = datetime.fromtimestamp(min(timestamps)).strftime("%Y-%m-%d %H:%M")
        end = datetime.fromtimestamp(max(timestamps)).strftime("%Y-%m-%d %H:%M")
        print(f"Period: {start} to {end}")
    print(f"Process memory (RSS): {span(rss, 'KB')}")
    print(f"Available system memory: {span(memory, 'KB')}")
    print(f"Battery readings: {span(battery, '%')}")
    if not rss or not memory:
        missing = []
        if not rss:
            missing.append(rss_key)
        if not memory:
            missing.append("mem_available_kb")
        print("INVALID TELEMETRY: no valid " + ", ".join(missing) + " samples were recorded.")
        return None, 2
    print(f"Last event: {rows[-1].get('event', 'unknown')} on {rows[-1].get('screen', rows[-1].get('launcher', 'unknown'))}")
    if len(rss) >= 10 and rss[-1] > rss[0] + 4096:
        print("Note: process memory rose by more than 4 MB across this log. Send this file for review.")
    else:
        print("Tip: a leak usually looks like process memory steadily rising across repeated use, not one small change.")
    return rss[-1] - rss[0] if len(rss) >= 2 else None, 0


def main() -> int:
    comparison_mode = len(sys.argv) == 3 and sys.argv[1] == "--compare"
    onion_mode = len(sys.argv) == 3 and sys.argv[1] == "--onion"
    sd_root = Path(sys.argv[2] if (onion_mode or comparison_mode) else sys.argv[1]) if len(sys.argv) in (2, 3) else Path.cwd()
    log_dir = sd_root / ".tmp_update" / "logs"
    if comparison_mode:
        pocket_delta, pocket_code = report(log_dir / "pocketos_comparison_health.csv", "PocketOS paired comparison report", "launcher_rss_kb")
        print()
        onion_delta, onion_code = report(log_dir / "onion_comparison_health.csv", "Onion paired comparison report", "launcher_rss_kb")
        if pocket_delta is not None and onion_delta is not None:
            print()
            print(f"RSS change: PocketOS {pocket_delta:+,} KB; Onion {onion_delta:+,} KB")
            print("These are comparable only when both runs used the same routine and device settings.")
        return 1 if pocket_code or onion_code else 0
    filename = "onion_baseline_health.csv" if onion_mode else "pocketos_health.csv"
    return report(log_dir / filename, "Onion baseline report" if onion_mode else "PocketOS health report", "launcher_rss_kb" if onion_mode else "rss_kb")[1]


if __name__ == "__main__":
    raise SystemExit(main())
