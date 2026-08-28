#!/usr/bin/env python3
"""Validate PocketOS build/release environment assumptions."""

from __future__ import annotations

import argparse
import hashlib
import json
import platform
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SPEC = ROOT / "pocketos" / "environment.json"


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _tool_version(tool: str) -> str | None:
    command = {
        "python3": [sys.executable, "--version"],
        "docker": ["docker", "--version"],
        "gcc": ["gcc", "--version"],
        "unzip": ["unzip", "-v"],
        "zip": ["zip", "--version"],
        "sdl-config": ["sdl-config", "--version"],
        "convert": ["convert", "--version"],
        "git": ["git", "--version"],
        "bash": ["bash", "--version"],
    }.get(tool, [tool, "--version"])
    try:
        result = subprocess.run(
            command,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=5,
            check=False,
        )
    except (OSError, subprocess.SubprocessError):
        return None
    first_line = result.stdout.splitlines()[0] if result.stdout else ""
    return first_line.strip() or None


def load_spec(path: Path) -> dict[str, Any]:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise SystemExit(f"invalid environment spec JSON: {path}: {exc}") from exc


def check_environment(spec: dict[str, Any], profile: str, root: Path = ROOT) -> dict[str, Any]:
    profiles = spec.get("profiles", {})
    if profile not in profiles:
        known = ", ".join(sorted(profiles)) or "<none>"
        raise SystemExit(f"unknown profile {profile!r}; known profiles: {known}")

    profile_spec = profiles[profile]
    missing_tools: list[str] = []
    tool_report: dict[str, dict[str, str | None]] = {}
    for tool in profile_spec.get("required_tools", []):
        path = shutil.which(tool)
        if path is None:
            missing_tools.append(tool)
            version = None
        else:
            version = _tool_version(tool)
        tool_report[tool] = {"path": path, "version": version}

    missing_paths: list[str] = []
    path_hashes: dict[str, str] = {}
    for rel in profile_spec.get("required_paths", []):
        path = root / rel
        if not path.exists():
            missing_paths.append(rel)
        elif path.is_file():
            path_hashes[rel] = _sha256(path)

    source_mismatches: list[dict[str, str]] = []
    for check in profile_spec.get("source_contains", []):
        rel = check["path"]
        expected = check["text"]
        path = root / rel
        if not path.is_file():
            source_mismatches.append({"path": rel, "reason": "missing"})
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        if expected not in text:
            source_mismatches.append({"path": rel, "reason": f"missing text: {expected}"})

    failures = {
        "missing_tools": missing_tools,
        "missing_paths": missing_paths,
        "source_mismatches": source_mismatches,
    }
    compatible = not any(failures.values())

    return {
        "environment_id": spec.get("environment_id"),
        "spec_version": spec.get("version"),
        "profile": profile,
        "status": "COMPATIBLE" if compatible else "INCOMPATIBLE",
        "project": spec.get("project", {}),
        "toolchain": spec.get("toolchain", {}),
        "host": {
            "platform": platform.platform(),
            "python": sys.version.split()[0],
        },
        "tools": tool_report,
        "path_hashes": path_hashes,
        "failures": failures,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", default="ci-arm", help="profile from pocketos/environment.json")
    parser.add_argument("--spec", type=Path, default=DEFAULT_SPEC, help="environment spec path")
    parser.add_argument("--json", action="store_true", help="print the full machine-readable report")
    args = parser.parse_args(argv)

    report = check_environment(load_spec(args.spec), args.profile)
    if args.json:
        print(json.dumps(report, indent=2, sort_keys=True))
    else:
        print(f"{report['environment_id']}:{report['profile']} {report['status']}")
        failures = report["failures"]
        for key in ("missing_tools", "missing_paths", "source_mismatches"):
            if failures[key]:
                print(f"{key}: {failures[key]}")
    return 0 if report["status"] == "COMPATIBLE" else 1


if __name__ == "__main__":
    raise SystemExit(main())
