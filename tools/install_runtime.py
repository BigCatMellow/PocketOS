#!/usr/bin/env python3
"""Activate PocketOS after extracting the manual SD-card archive."""

from __future__ import annotations

import argparse
import os
import shutil
from pathlib import Path

try:
    from .onion_runtime import RuntimePatchError, install_runtime_hook
except ImportError:  # Direct script and release archive execution.
    from onion_runtime import RuntimePatchError, install_runtime_hook


def install_payload(payload_root: Path, sd_root: Path) -> None:
    payload_root = payload_root.resolve()
    sd_root = sd_root.resolve()
    source_binary = payload_root / ".tmp_update" / "bin" / "pocketOS"
    source_resources = payload_root / ".tmp_update" / "res" / "pocketos"
    source_test_center = payload_root / "App" / "PocketOS Test Center"
    if not source_binary.is_file():
        raise FileNotFoundError(f"PocketOS binary not found: {source_binary}")
    if not source_resources.is_dir():
        raise FileNotFoundError(f"PocketOS resources not found: {source_resources}")

    if payload_root == sd_root:
        return

    destination_resources = sd_root / ".tmp_update" / "res" / "pocketos"
    resource_staging = destination_resources.with_name(".pocketos.installing")
    resource_backup = destination_resources.with_name(".pocketos.previous")
    for path in (resource_staging, resource_backup):
        if path.exists():
            shutil.rmtree(path)
    shutil.copytree(source_resources, resource_staging)
    active_theme = destination_resources / "theme.json"
    if active_theme.is_file():
        shutil.copy2(active_theme, resource_staging / "theme.json")
    if destination_resources.exists():
        destination_resources.rename(resource_backup)
    try:
        resource_staging.rename(destination_resources)
    except Exception:
        if resource_backup.exists() and not destination_resources.exists():
            resource_backup.rename(destination_resources)
        raise
    if resource_backup.exists():
        shutil.rmtree(resource_backup)

    binary_dir = sd_root / ".tmp_update" / "bin"
    binary_dir.mkdir(parents=True, exist_ok=True)
    destination_binary = binary_dir / "pocketOS"
    binary_staging = binary_dir / ".pocketOS.installing"
    if binary_staging.exists():
        binary_staging.unlink()
    shutil.copy2(source_binary, binary_staging)
    os.replace(binary_staging, destination_binary)
    destination_binary.chmod(0o755)

    if source_test_center.is_dir():
        destination_test_center = sd_root / "App" / "PocketOS Test Center"
        staging_test_center = destination_test_center.with_name(".PocketOS Test Center.installing")
        if staging_test_center.exists():
            shutil.rmtree(staging_test_center)
        shutil.copytree(source_test_center, staging_test_center)
        if destination_test_center.exists():
            shutil.rmtree(destination_test_center)
        staging_test_center.rename(destination_test_center)
        (destination_test_center / "launch.sh").chmod(0o755)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Install PocketOS's fail-open launcher hook into Onion OS."
    )
    parser.add_argument(
        "sd_root",
        nargs="?",
        type=Path,
        default=Path(__file__).resolve().parent,
        help="SD-card root (defaults to the directory containing this script)",
    )
    args = parser.parse_args()
    payload_root = Path(__file__).resolve().parent
    sd_root = args.sd_root.resolve()
    try:
        install_payload(payload_root, sd_root)
        changed = install_runtime_hook(sd_root)
    except (OSError, RuntimePatchError) as exc:
        parser.error(str(exc))
    print("PocketOS launcher hook installed." if changed else "PocketOS launcher hook already installed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
