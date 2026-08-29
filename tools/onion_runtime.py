"""Install and remove PocketOS's fail-open Onion runtime hook."""

from __future__ import annotations

import os
import shutil
import tempfile
from pathlib import Path


RUNTIME_REL = Path(".tmp_update/runtime.sh")
BACKUP_REL = Path(".tmp_update/runtime.sh.before-pocketos")
BEGIN_MARKER = "# POCKETOS_INTEGRATION_BEGIN"
END_MARKER = "# POCKETOS_INTEGRATION_END"
LAUNCH_HEADING = "# MainUI launch"
POST_LAUNCH_HEADING = "# Merge the last game launched into the recent list"

MANAGED_BLOCK = """\
    # POCKETOS_INTEGRATION_BEGIN
    cd "$miyoodir/app"
    pocketos_fail_flag=${POCKETOS_FAIL_FLAG:-/tmp/pocketos_failed}
    if [ -x "$sysdir/bin/pocketOS" ] && [ ! -e "$pocketos_fail_flag" ]; then
        PATH="$miyoodir/app:$PATH" \\
            LD_LIBRARY_PATH="$miyoodir/lib:/config/lib:/lib" \\
            LD_PRELOAD="$miyoodir/lib/libpadsp.so" \\
            "$sysdir/bin/pocketOS"
        pocketos_status=$?
        if [ "$pocketos_status" -ne 0 ]; then
            mkdir -p "$sysdir/logs"
            printf '%s\\n' "PocketOS failed with status $pocketos_status; using MainUI for this boot" \\
                >> "$sysdir/logs/pocketos_runtime.log"
            : > "$pocketos_fail_flag"
            PATH="$miyoodir/app:$PATH" \\
                LD_LIBRARY_PATH="$miyoodir/lib:/config/lib:/lib" \\
                LD_PRELOAD="$miyoodir/lib/libpadsp.so" \\
                ./MainUI > /dev/null 2>&1
        fi
    else
        PATH="$miyoodir/app:$PATH" \\
            LD_LIBRARY_PATH="$miyoodir/lib:/config/lib:/lib" \\
            LD_PRELOAD="$miyoodir/lib/libpadsp.so" \\
            ./MainUI > /dev/null 2>&1
    fi
    # POCKETOS_INTEGRATION_END
"""

STOCK_BLOCK = """\
    cd "$miyoodir/app"
    PATH="$miyoodir/app:$PATH" \\
        LD_LIBRARY_PATH="$miyoodir/lib:/config/lib:/lib" \\
        LD_PRELOAD="$miyoodir/lib/libpadsp.so" \\
        ./MainUI > /dev/null 2>&1
"""


class RuntimePatchError(RuntimeError):
    """Raised when an Onion runtime cannot be patched without guessing."""


def _line_after(text: str, needle: str, start: int = 0) -> int:
    index = text.find(needle, start)
    if index < 0:
        raise RuntimePatchError(f"Onion runtime is missing expected marker: {needle}")
    newline = text.find("\n", index)
    return len(text) if newline < 0 else newline + 1


def _mainui_launch_bounds(text: str):
    heading = text.find(LAUNCH_HEADING)
    if heading < 0:
        raise RuntimePatchError("Onion runtime does not contain a MainUI launch section")
    start = _line_after(text, LAUNCH_HEADING, heading)
    post = text.find(POST_LAUNCH_HEADING, start)
    if post < 0:
        raise RuntimePatchError("Onion runtime does not contain the post-MainUI section")
    separator = text.rfind("\n", start, post)
    if separator < start:
        raise RuntimePatchError("Onion runtime has an invalid MainUI launch section")
    return start, separator + 1


def patch_runtime_text(text: str) -> str:
    """Return runtime text with an idempotent, fail-open PocketOS launcher."""
    if BEGIN_MARKER in text or END_MARKER in text:
        if text.count(BEGIN_MARKER) != 1 or text.count(END_MARKER) != 1:
            raise RuntimePatchError("Onion runtime has incomplete PocketOS markers")
        begin = text.index(BEGIN_MARKER)
        end = _line_after(text, END_MARKER, begin)
        indent_start = text.rfind("\n", 0, begin) + 1
        return text[:indent_start] + MANAGED_BLOCK + text[end:]

    start, end = _mainui_launch_bounds(text)
    return text[:start] + MANAGED_BLOCK + "\n" + text[end:]


def unpatch_runtime_text(text: str, stock_block: str = STOCK_BLOCK) -> str:
    """Return runtime text with the managed hook replaced by stock MainUI launch."""
    if BEGIN_MARKER not in text and END_MARKER not in text:
        return text
    if text.count(BEGIN_MARKER) != 1 or text.count(END_MARKER) != 1:
        raise RuntimePatchError("Onion runtime has incomplete PocketOS markers")
    begin = text.index(BEGIN_MARKER)
    end = _line_after(text, END_MARKER, begin)
    start = text.rfind("\n", 0, begin) + 1
    return text[:start] + stock_block + text[end:]


def _atomic_write(path: Path, text: str) -> None:
    mode = path.stat().st_mode
    fd, tmp_name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    tmp = Path(tmp_name)
    try:
        with os.fdopen(fd, "w", newline="") as handle:
            handle.write(text)
            handle.flush()
            os.fsync(handle.fileno())
        os.chmod(tmp, mode)
        os.replace(tmp, path)
    finally:
        if tmp.exists():
            tmp.unlink()


def install_runtime_hook(sd_root: Path) -> bool:
    """Install the hook. Return True when runtime.sh changed."""
    runtime = Path(sd_root) / RUNTIME_REL
    if not runtime.is_file():
        raise RuntimePatchError(f"Onion runtime not found: {runtime}")
    original = runtime.read_text(encoding="utf-8")
    patched = patch_runtime_text(original)
    if patched == original:
        return False
    backup = Path(sd_root) / BACKUP_REL
    # This call is patching a stock/unmanaged runtime, so it is the most current
    # rollback source. Refresh atomically instead of retaining a stale Onion copy.
    backup_tmp = backup.with_name(f".{backup.name}.installing")
    shutil.copy2(runtime, backup_tmp)
    os.replace(backup_tmp, backup)
    _atomic_write(runtime, patched)
    return True


def remove_runtime_hook(sd_root: Path) -> bool:
    """Remove only the managed hook. Return True when runtime.sh changed."""
    runtime = Path(sd_root) / RUNTIME_REL
    if not runtime.is_file():
        raise RuntimePatchError(f"Onion runtime not found: {runtime}")
    original = runtime.read_text(encoding="utf-8")
    stock_block = STOCK_BLOCK
    backup = Path(sd_root) / BACKUP_REL
    if backup.is_file():
        try:
            backup_text = backup.read_text(encoding="utf-8")
            start, end = _mainui_launch_bounds(backup_text)
            stock_block = backup_text[start:end - 1]
        except (OSError, RuntimePatchError):
            pass
    restored = unpatch_runtime_text(original, stock_block)
    if restored == original:
        return False
    _atomic_write(runtime, restored)
    return True
