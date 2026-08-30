"""Load PocketOS genre overrides without executing user-controlled code."""

from __future__ import annotations

import json
import sys
from pathlib import Path

OVERRIDE_FILENAME = "genre_overrides.json"


def _candidate_paths() -> list[Path]:
    paths: list[Path] = []
    if getattr(sys, "frozen", False):
        # A user-editable file beside the executable wins over bundled defaults.
        paths.append(Path(sys.executable).parent / OVERRIDE_FILENAME)
        bundle_root = getattr(sys, "_MEIPASS", None)
        if bundle_root:
            paths.append(Path(bundle_root) / OVERRIDE_FILENAME)
    paths.append(Path(__file__).parent / OVERRIDE_FILENAME)
    return paths


def load_overrides() -> dict[str, str]:
    for path in _candidate_paths():
        if not path.is_file():
            continue
        try:
            raw = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            continue
        if not isinstance(raw, dict):
            continue
        result = {}
        for key, value in raw.items():
            if isinstance(key, str) and isinstance(value, str) and key.strip() and value.strip():
                result[key] = value
        return result
    return {}
