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


def main() -> int:
    onion_mode = len(sys.argv) == 3 and sys.argv[1] == "--onion"
    sd_root = Path(sys.argv[2] if onion_mode else sys.argv[1]) if len(sys.argv) in (2, 3) else Path.cwd()
    filename = "onion_baseline_health.csv" if onion_mode else "pocketos_health.csv"
    path = sd_root / ".tmp_update" / "logs" / filename
    if not path.is_file():
        print("No health log found.")
        print(f"Expected: {path}")
        return 1

    with path.open(newline="", encoding="utf-8") as handle:
        rows = list(csv.DictReader(handle))
    if not rows:
        print("The health log is enabled but has no samples yet. Use PocketOS for a few minutes, then try again.")
        return 0

    timestamps = [number(row, "timestamp") for row in rows]
    timestamps = [value for value in timestamps if value is not None]
    rss_key = "launcher_rss_kb" if onion_mode else "rss_kb"
    rss = [number(row, rss_key) for row in rows]
    memory = [number(row, "mem_available_kb") for row in rows]
    battery = [number(row, "battery_percent") for row in rows]
    rss = [value for value in rss if value is not None]
    memory = [value for value in memory if value is not None]
    battery = [value for value in battery if value is not None]

    print("Onion baseline report" if onion_mode else "PocketOS health report")
    print("=" * 22)
    print(f"Samples: {len(rows)}")
    if timestamps:
        start = datetime.fromtimestamp(min(timestamps)).strftime("%Y-%m-%d %H:%M")
        end = datetime.fromtimestamp(max(timestamps)).strftime("%Y-%m-%d %H:%M")
        print(f"Period: {start} to {end}")
    print(f"Process memory (RSS): {span(rss, 'KB')}")
    print(f"Available system memory: {span(memory, 'KB')}")
    print(f"Battery readings: {span(battery, '%')}")
    print(f"Last event: {rows[-1].get('event', 'unknown')} on {rows[-1].get('screen', 'unknown')}")
    if len(rss) >= 10 and rss[-1] > rss[0] + 4096:
        print("Note: process memory rose by more than 4 MB across this log. Send this file for review.")
    else:
        print("Tip: a leak usually looks like process memory steadily rising across repeated use, not one small change.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
