#!/usr/bin/env python3
"""PocketOS Installer — terminal edition"""

import os
import re
import sys
import json
import shutil
import sqlite3
import tempfile
import urllib.request
import urllib.error
import webbrowser
import zipfile
import xml.etree.ElementTree as ET
from pathlib import Path

try:
    from .onion_runtime import BEGIN_MARKER, END_MARKER, install_runtime_hook, remove_runtime_hook
except ImportError:  # Direct script and PyInstaller execution.
    from onion_runtime import BEGIN_MARKER, END_MARKER, install_runtime_hook, remove_runtime_hook

try:
    from .onion_systems import ROM_EXTENSIONS, candidates_for_extension, openvgdb_system_name
except ImportError:  # Direct script and PyInstaller execution.
    from onion_systems import ROM_EXTENSIONS, candidates_for_extension, openvgdb_system_name

try:
    from .genre_overrides import load_overrides as load_genre_overrides
except ImportError:  # Direct script and PyInstaller execution.
    from genre_overrides import load_overrides as load_genre_overrides

try:
    from .rom_safety import (
        GamelistError, crc32_of, extract_zip_roms, index_games,
        load_gamelist_tree, write_xml_atomic,
    )
except ImportError:  # Direct script and PyInstaller execution.
    from rom_safety import (
        GamelistError, crc32_of, extract_zip_roms, index_games,
        load_gamelist_tree, write_xml_atomic,
    )

# ── Bundled assets path ───────────────────────────────────────────────────────
if getattr(sys, "frozen", False):
    BASE_DIR = Path(sys._MEIPASS) / "payload"
else:
    BASE_DIR = Path(os.environ.get(
        "POCKETOS_PAYLOAD",
        Path(__file__).parent.parent / "payload",
    ))

PAYLOAD_BIN = BASE_DIR / ".tmp_update" / "bin" / "pocketOS"
PAYLOAD_RES = BASE_DIR / ".tmp_update" / "res" / "pocketos"
PAYLOAD_HEALTH_REPORT = BASE_DIR / "pocketos-health-report.py"
PAYLOAD_STRESS_TEST = BASE_DIR / "pocketos-stress-test.sh"
PAYLOAD_ONION_MONITOR = BASE_DIR / "onion-baseline-monitor.sh"
PAYLOAD_COMPARISON_MONITOR = BASE_DIR / "launcher-comparison-monitor.sh"
RUNTIME_REL = Path(".tmp_update") / "runtime.sh"

ONION_URL   = "https://github.com/OnionUI/Onion/releases/latest"
GITHUB_API  = "https://api.github.com/repos/BigCatMellow/PocketOS/releases/latest"
GITHUB_REPO = "https://github.com/BigCatMellow/PocketOS/releases/latest"

VERSION = "v1.2.8"


# ── ROM import constants ──────────────────────────────────────────────────────

ROM_EXTS = set(ROM_EXTENSIONS)

DOC_NAMES = {"readme", "license", "changelog", "credits", "notes", "info", "manual"}

# ── Genre scan SQL ────────────────────────────────────────────────────────────

QUERY_CRC = """
    SELECT r.releaseTitleName, r.releaseGenre
    FROM RELEASES r JOIN ROMs ro ON r.romID = ro.romID
    JOIN SYSTEMS s ON ro.systemID = s.systemID
    WHERE UPPER(ro.romHashCRC) = ? AND s.systemName = ? LIMIT 1
"""
QUERY_FILENAME = """
    SELECT r.releaseTitleName, r.releaseGenre
    FROM RELEASES r JOIN ROMs ro ON r.romID = ro.romID
    JOIN SYSTEMS s ON ro.systemID = s.systemID
    WHERE ro.romExtensionlessFileName = ? AND s.systemName = ? LIMIT 1
"""


# ── PocketOS install helpers ──────────────────────────────────────────────────

def detect_sd(path: Path) -> bool:
    return (path / ".tmp_update").is_dir() and (path / "Roms").is_dir()

def detect_onion(path: Path) -> bool:
    return (path / "miyoo" / "app" / "MainUI").exists() or \
           (path / ".tmp_update" / "onionVersion" / "version.txt").exists() or \
           (path / ".tmp_update" / "onion_version").exists() or \
           (path / "BIOS").is_dir()

def audit_install(sd: Path, payload: Path = BASE_DIR) -> tuple[list[str], list[str]]:
    errors = []
    warnings = []
    if not detect_sd(sd):
        errors.append("SD root must contain Roms/ and .tmp_update/.")
    if not detect_onion(sd):
        warnings.append("Onion OS was not confidently detected; install requires Onion OS.")

    bin_src = payload / ".tmp_update" / "bin" / "pocketOS"
    res_src = payload / ".tmp_update" / "res" / "pocketos"
    if not bin_src.is_file():
        errors.append(f"PocketOS payload binary is missing: {bin_src}")
    if not res_src.is_dir():
        errors.append(f"PocketOS payload resources are missing: {res_src}")

    runtime = sd / RUNTIME_REL
    if not runtime.is_file():
        errors.append(f"Onion runtime is missing: {runtime}")
    else:
        text = runtime.read_text(encoding="utf-8", errors="replace")
        begin_count = text.count(BEGIN_MARKER)
        end_count = text.count(END_MARKER)
        if begin_count != end_count:
            errors.append("PocketOS runtime markers are incomplete; uninstall or restore runtime.sh first.")
        elif begin_count > 1:
            errors.append("PocketOS runtime markers appear more than once; runtime.sh needs manual cleanup.")
        if "# MainUI launch" not in text and begin_count == 0:
            errors.append("Onion runtime does not contain the expected MainUI launch section.")
    return errors, warnings

def fetch_latest_release():
    try:
        req = urllib.request.Request(
            GITHUB_API,
            headers={"User-Agent": "PocketOS-Installer", "Accept": "application/vnd.github+json"}
        )
        with urllib.request.urlopen(req, timeout=6) as r:
            data = json.loads(r.read())
        tag = data.get("tag_name", "")
        zip_url = next(
            (a["browser_download_url"] for a in data.get("assets", [])
             if a["name"].endswith(".zip") and "pocketOS" in a["name"]), None
        )
        return tag, zip_url
    except Exception:
        return None, None

def version_tuple(v: str):
    return tuple(int(x) for x in v.lstrip("v").split(".") if x.isdigit())

def _copy_file_atomic(src: Path, dest: Path):
    tmp = dest.with_name(f".{dest.name}.installing")
    if tmp.exists():
        tmp.unlink()
    shutil.copy2(src, tmp)
    os.replace(tmp, dest)

POCKETOS_TRANSACTION_PATHS = (
    Path(".tmp_update/res/pocketos"),
    Path(".tmp_update/bin/pocketOS"),
    Path("pocketos-health-report.py"),
    Path("pocketos-stress-test.sh"),
    Path("onion-baseline-monitor.sh"),
    Path("launcher-comparison-monitor.sh"),
    Path(".tmp_update/runtime.sh"),
    Path(".tmp_update/runtime.sh.before-pocketos"),
)


def _remove_snapshot_target(path: Path):
    if path.is_symlink() or path.is_file():
        path.unlink()
    elif path.is_dir():
        shutil.rmtree(path)


class _InstallTransaction:
    """Rollback guard for the small set of paths PocketOS owns or mutates."""

    def __init__(self, sd: Path):
        self.sd = sd
        tmp_root = sd / ".tmp_update"
        tmp_root.mkdir(parents=True, exist_ok=True)
        self.root = Path(tempfile.mkdtemp(prefix=".pocketos-transaction-", dir=tmp_root))
        self.entries = []
        self.parent_state = {
            sd / ".tmp_update" / "bin": (sd / ".tmp_update" / "bin").exists(),
            sd / ".tmp_update" / "res": (sd / ".tmp_update" / "res").exists(),
        }
        for index, relative in enumerate(POCKETOS_TRANSACTION_PATHS):
            target = sd / relative
            existed = target.exists() or target.is_symlink()
            backup = self.root / str(index)
            kind = None
            link_target = None
            if existed:
                if target.is_symlink():
                    kind = "symlink"
                    link_target = os.readlink(target)
                elif target.is_dir():
                    kind = "dir"
                    shutil.copytree(target, backup, symlinks=True)
                else:
                    kind = "file"
                    shutil.copy2(target, backup, follow_symlinks=False)
            self.entries.append((target, existed, kind, backup, link_target))

    def rollback(self):
        for target, existed, kind, backup, link_target in reversed(self.entries):
            if target.exists() or target.is_symlink():
                _remove_snapshot_target(target)
            if not existed:
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            if kind == "dir":
                shutil.copytree(backup, target, symlinks=True)
            elif kind == "symlink":
                os.symlink(link_target, target)
            else:
                shutil.copy2(backup, target, follow_symlinks=False)
        for parent, existed in self.parent_state.items():
            if not existed and parent.is_dir():
                try:
                    parent.rmdir()
                except OSError:
                    pass
        self._cleanup()

    def commit(self):
        self._cleanup()

    def _cleanup(self):
        if self.root.exists():
            shutil.rmtree(self.root)


def _remove_owned_file(path: Path, log, label: str):
    if path.exists() or path.is_symlink():
        path.unlink()
        log(f"  Removed {label}")


def _replace_tree(src: Path, dest: Path, preserve=()):
    staging = dest.with_name(f".{dest.name}.installing")
    backup = dest.with_name(f".{dest.name}.previous")
    for path in (staging, backup):
        if path.exists():
            shutil.rmtree(path)
    shutil.copytree(src, staging)
    for relative in preserve:
        existing = dest / relative
        if existing.is_file():
            target = staging / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(existing, target)
    if dest.exists():
        dest.rename(backup)
    try:
        staging.rename(dest)
    except Exception:
        if backup.exists() and not dest.exists():
            backup.rename(dest)
        raise
    if backup.exists():
        shutil.rmtree(backup)

def install_from_dir(src: Path, sd: Path, log):
    errors, warnings = audit_install(sd, src)
    for warning in warnings:
        log(f"  WARNING: {warning}")
    if errors:
        raise RuntimeError("; ".join(errors))

    bin_src = src / ".tmp_update" / "bin" / "pocketOS"
    res_src = src / ".tmp_update" / "res" / "pocketos"
    bin_dest = sd / ".tmp_update" / "bin"
    res_dest = sd / ".tmp_update" / "res" / "pocketos"
    report_dest = sd / "pocketos-health-report.py"
    stress_dest = sd / "pocketos-stress-test.sh"
    onion_monitor_dest = sd / "onion-baseline-monitor.sh"
    comparison_monitor_dest = sd / "launcher-comparison-monitor.sh"
    if not bin_src.exists():
        raise FileNotFoundError(f"Binary not found: {bin_src}")
    if not res_src.is_dir():
        raise FileNotFoundError(f"Assets not found: {res_src}")

    transaction = _InstallTransaction(sd)
    try:
        log("  Setting up folders on SD card...")
        bin_dest.mkdir(parents=True, exist_ok=True)
        log("  Copying themes, icons, and fonts...")
        _replace_tree(res_src, res_dest, preserve=(Path("theme.json"),))
        if PAYLOAD_HEALTH_REPORT.is_file():
            _copy_file_atomic(PAYLOAD_HEALTH_REPORT, report_dest)
        if PAYLOAD_STRESS_TEST.is_file():
            _copy_file_atomic(PAYLOAD_STRESS_TEST, stress_dest)
            stress_dest.chmod(0o755)
        if PAYLOAD_ONION_MONITOR.is_file():
            _copy_file_atomic(PAYLOAD_ONION_MONITOR, onion_monitor_dest)
            onion_monitor_dest.chmod(0o755)
        if PAYLOAD_COMPARISON_MONITOR.is_file():
            _copy_file_atomic(PAYLOAD_COMPARISON_MONITOR, comparison_monitor_dest)
            comparison_monitor_dest.chmod(0o755)
        log("  Copying PocketOS launcher...")
        _copy_file_atomic(bin_src, bin_dest / "pocketOS")
        (bin_dest / "pocketOS").chmod(0o755)
        log("  Installing fail-open Onion launcher hook...")
        install_runtime_hook(sd)
        errors, _warnings = audit_install(sd, src)
        if errors:
            raise RuntimeError("Post-install audit failed: " + "; ".join(errors))
    except Exception:
        transaction.rollback()
        raise
    else:
        transaction.commit()

def install(sd: Path, log):
    install_from_dir(BASE_DIR, sd, log)

def uninstall(sd: Path, log):
    transaction = _InstallTransaction(sd)
    try:
        log("  Restoring the stock Onion launcher...")
        remove_runtime_hook(sd)
        log("  Removing PocketOS launcher...")
        _remove_owned_file(sd / ".tmp_update" / "bin" / "pocketOS", log, "launcher binary")
        log("  Removing themes and assets...")
        res = sd / ".tmp_update" / "res" / "pocketos"
        if res.exists():
            shutil.rmtree(res)
            log("  Removed themes, icons, and fonts")
        for filename, label in (
            ("pocketos-health-report.py", "health report helper"),
            ("pocketos-stress-test.sh", "stress-test helper"),
            ("onion-baseline-monitor.sh", "Onion baseline monitor"),
            ("launcher-comparison-monitor.sh", "launcher comparison monitor"),
        ):
            _remove_owned_file(sd / filename, log, label)
    except Exception:
        transaction.rollback()
        raise
    else:
        transaction.commit()


# ── ROM import helpers ────────────────────────────────────────────────────────

def _is_doc_file(name: str) -> bool:
    stem = Path(name).stem.lower()
    return stem in DOC_NAMES or any(stem.startswith(d) for d in DOC_NAMES)

def detect_system(zip_path: Path):
    """Return a safe extension-derived Onion target; ambiguous formats are skipped."""
    try:
        with zipfile.ZipFile(zip_path) as zf:
            for name in (n for n in zf.namelist() if not n.endswith("/")):
                if _is_doc_file(name):
                    continue
                ext = Path(name).suffix.lower()
                candidates = candidates_for_extension(ext)
                if candidates:
                    return ext, list(candidates)
    except Exception:
        pass
    return None, []

def find_system_folder(roms_root: Path, candidates: list):
    for name in candidates:
        p = roms_root / name
        if p.is_dir():
            return p
    return None

def extract_zip(zip_path: Path, dest_folder: Path, allowed_extensions, log) -> list:
    """Use the shared preflighted, streamed, atomic ZIP extractor."""
    return extract_zip_roms(zip_path, dest_folder, set(allowed_extensions), log)


# ── Variant cleanup ───────────────────────────────────────────────────────────

def _base_name(stem: str) -> str:
    m = re.search(r' [\(\[]', stem)
    return stem[:m.start()].strip().lower() if m else stem.strip().lower()

def _rom_score(name: str) -> int:
    n    = name.upper()
    _end = r'(?:[ \[\(\.]|$)'
    score = 0
    if '[!]'     in n:                              score += 100
    if re.search(r'\(USA\)|\(U\)' + _end,   n):    score +=  20
    if '(WORLD)' in n:                              score +=  15
    if re.search(r'\(EUROPE\)|\(E\)' + _end, n):   score +=  10
    if re.search(r'\(JAPAN\)|\(J\)' + _end,  n):   score +=   5
    if re.search(r'\[B',   n):                      score -= 1000
    if re.search(r'\[O',   n):                      score -=  500
    if re.search(r'\[H',   n):                      score -=  200
    if re.search(r'\[T\d', n):                      score -=  150
    if re.search(r'\[T[+\-]', n):                   score -=   80
    if re.search(r'\[A\d', n):                      score -=   50
    if re.search(r'\[F\d', n):                      score -=   30
    if re.search(r'\[P\d', n):                      score -=  100
    if '(PD)'    in n:                              score -=   20
    if '(PIRATE)' in n:                             score -=  100
    if re.search(r'\bHACK\b',    n):                score -=  150
    if re.search(r'\bTRAINER\b', n):                score -=  100
    return score

def clean_variants(folder: Path, log) -> int:
    """Report likely variants without deleting or moving user files."""
    rom_files = [f for f in sorted(folder.iterdir())
                 if f.is_file() and f.suffix.lower() in ROM_EXTS]
    groups: dict = {}
    for f in rom_files:
        groups.setdefault(_base_name(f.stem), []).append(f)
    flagged = 0
    for files in groups.values():
        if len(files) == 1:
            continue
        scored = sorted(files, key=lambda f: (-_rom_score(f.name), f.name))
        log(f"    suggested keep: {scored[0].name}")
        for f in scored[1:]:
            log(f"    possible variant (no action): {f.name}")
            flagged += 1
    return flagged


# ── Genre scan helpers ────────────────────────────────────────────────────────

def _crc32_of(path: Path) -> str:
    return crc32_of(path)

def _db_lookup(db, rom: Path, system_name: str):
    crc = _crc32_of(rom)
    if crc:
        row = db.execute(QUERY_CRC, (crc, system_name)).fetchone()
        if row and row[0]:
            return row[0], row[1] or "Unsorted"
    row = db.execute(QUERY_FILENAME, (rom.stem, system_name)).fetchone()
    if row and row[0]:
        return row[0], row[1] or "Unsorted"
    return None

def scan_genres_for_system(roms_root: Path, system_folder: str, db_path: Path, log) -> int:
    system_dir = roms_root / system_folder
    gamelist = system_dir / "miyoogamelist.xml"
    system_name = openvgdb_system_name(system_folder)
    if not system_name:
        return 0
    try:
        db = sqlite3.connect(str(db_path))
    except Exception as e:
        log(f"    DB open failed: {e}")
        return 0

    try:
        tree = load_gamelist_tree(gamelist)
    except GamelistError as e:
        db.close()
        log(f"    ERROR: {e}")
        return 0
    root = tree.getroot()
    existing = index_games(root)

    added = 0
    for rom in sorted(system_dir.iterdir()):
        if not rom.is_file() or rom.suffix.lower() not in ROM_EXTS:
            continue
        if rom.name in existing:
            continue
        rom_system_name = openvgdb_system_name(system_folder, rom.suffix) or system_name
        result = _db_lookup(db, rom, rom_system_name)
        name, genre = result if result else (rom.stem, "Unsorted")
        el = ET.SubElement(root, "game")
        ET.SubElement(el, "path").text = "./" + rom.name
        ET.SubElement(el, "name").text = name
        ET.SubElement(el, "genre").text = genre
        added += 1
    db.close()
    if added:
        write_xml_atomic(tree, gamelist)
    return added

def apply_overrides(roms_root: Path, system_folder: str, overrides: dict, log) -> int:
    gamelist = roms_root / system_folder / "miyoogamelist.xml"
    if not gamelist.exists():
        return 0
    try:
        tree = load_gamelist_tree(gamelist)
    except GamelistError as exc:
        log(f"    ERROR: {exc}")
        return 0
    root    = tree.getroot()
    changed = 0
    for game in root.findall("game"):
        genre_el = game.find("genre")
        if genre_el is None or genre_el.text != "Unsorted":
            continue
        name = game.findtext("name") or ""
        if name in overrides:
            genre_el.text = overrides[name]
            changed += 1
    if changed:
        write_xml_atomic(tree, gamelist)
    return changed

def _asset_dir() -> Path:
    """Directory to search for loose data files (openvgdb, overrides).
    When frozen: folder containing the executable.
    When running from source: the tools/ directory."""
    if getattr(sys, "frozen", False):
        return Path(sys.executable).parent
    return Path(__file__).parent

def _load_overrides() -> dict:
    return load_genre_overrides()

def _find_db() -> Path | None:
    p = _asset_dir() / "openvgdb.sqlite"
    return p if p.exists() else None


# ── Terminal UI ───────────────────────────────────────────────────────────────

# ANSI colours
_R    = "\033[0m"
_BOLD = "\033[1m"
_LAV  = "\033[38;5;183m"
_LIME = "\033[38;5;112m"
_AMBR = "\033[38;5;214m"
_RED  = "\033[38;5;196m"
_SOFT = "\033[38;5;246m"
_WHT  = "\033[38;5;255m"

def _ok(msg):   print(f"  \033[38;5;112m✓\033[0m  {msg}")
def _err(msg):  print(f"  \033[38;5;196m✗\033[0m  {msg}")
def _info(msg): print(f"  \033[38;5;183m›\033[0m  {msg}")
def _warn(msg): print(f"  \033[38;5;214m⚠\033[0m  {msg}")
def _head(msg): print(f"\n  {_BOLD}{_LAV}{msg}{_R}")

def _log(text):
    t = text.strip()
    if not t:
        print()
    elif t.startswith("✓"):
        _ok(t[1:].strip())
    elif t.startswith("✗") or "ERROR" in text:
        _err(t)
    elif t.startswith("──"):
        _head(t)
    else:
        _info(t)


def _select_candidate(candidates, input_fn=input, warn_fn=_warn):
    """Require an explicit valid choice; never infer a destructive target."""
    while True:
        raw = input_fn(f"\n  Select [1-{len(candidates)}]: ").strip()
        try:
            number = int(raw)
        except ValueError:
            number = 0
        if 1 <= number <= len(candidates):
            return candidates[number - 1]
        warn_fn(f"Enter a number from 1 to {len(candidates)}")


class Installer:

    def __init__(self):
        self._sd = None

    def run(self):
        self._print_header()
        self._check_update()
        self._detect_sd()
        self._main_menu()

    # ── Header ──

    def _print_header(self):
        print()
        print(f"  {_LAV}╔{'═'*34}╗{_R}")
        print(f"  {_LAV}║{_R}  {_BOLD}{_WHT}Pocket{_R}{_BOLD}{_LAV}OS{_R}"
              f"  Installer  {_SOFT}{VERSION}{_R}"
              f"          {_LAV}║{_R}")
        print(f"  {_LAV}╚{'═'*34}╝{_R}")
        print()

    # ── Update check ──

    def _check_update(self):
        try:
            tag, url = fetch_latest_release()
            if tag and url and version_tuple(tag) > version_tuple(VERSION):
                _warn(f"Update available: {_BOLD}{tag}{_R}  →  {GITHUB_REPO}")
                print()
        except Exception:
            pass

    # ── SD card detection ──

    def _detect_sd(self):
        _info("Scanning for Miyoo SD cards...")
        candidates = []
        if sys.platform == "win32":
            import string
            for letter in string.ascii_uppercase:
                p = Path(f"{letter}:\\")
                if p.exists() and detect_sd(p):
                    candidates.append(p)
        else:
            for mount in [Path("/media"), Path("/mnt"), Path("/Volumes")]:
                if not mount.exists():
                    continue
                try:
                    for child in mount.iterdir():
                        if detect_sd(child):
                            candidates.append(child)
                        for gc in (child.iterdir() if child.is_dir() else []):
                            if detect_sd(gc):
                                candidates.append(gc)
                except PermissionError:
                    pass

        if len(candidates) == 1:
            self._sd = candidates[0]
            _ok(f"Found: {_BOLD}{self._sd}{_R}")
        elif len(candidates) > 1:
            print()
            for i, p in enumerate(candidates):
                print(f"    [{i+1}] {p}")
            self._sd = _select_candidate(candidates)
            _ok(f"Selected: {_BOLD}{self._sd}{_R}")
        else:
            _warn("No SD card detected automatically.")
            path = input("  Enter path to SD card root: ").strip().strip('"')
            p = Path(path)
            if not p.is_dir() or not detect_sd(p):
                _err("Not a valid Miyoo SD card root (needs Roms/ and .tmp_update/).")
                sys.exit(1)
            self._sd = p
            _ok(f"Using: {_BOLD}{self._sd}{_R}")

        if not detect_onion(self._sd):
            print()
            _warn("Onion OS not detected on this card.")
            _info(f"PocketOS requires Onion OS: {_LAV}{ONION_URL}{_R}")
            print()

    # ── Main menu ──

    def _main_menu(self):
        print()
        print(f"  {_SOFT}Card:{_R} {self._sd}")
        print()
        print(f"  {_BOLD}{_LAV}[I]{_R}  Install PocketOS")
        print(f"  {_BOLD}{_RED}[U]{_R}  Uninstall PocketOS")
        print(f"  {_BOLD}{_SOFT}[Q]{_R}  Quit")
        print()

        while True:
            try:
                choice = input("  > ").strip().lower()
            except (EOFError, KeyboardInterrupt):
                print()
                sys.exit(0)
            if choice == "i":
                self._do_install()
                break
            elif choice == "u":
                self._do_uninstall()
                break
            elif choice in ("q", "quit", "exit", ""):
                print()
                sys.exit(0)
            else:
                _warn("Enter I, U, or Q")

    # ── Install ──

    def _do_install(self):
        errors, warnings = audit_install(self._sd)
        for warning in warnings:
            _warn(warning)
        if errors:
            _err("Install cannot continue:")
            for error in errors:
                _err(f"  {error}")
            _info("Re-download the current installer from the releases page if the payload is missing.")
            return

        print()
        do_import = input("  Import ROMs from a folder of ZIP files? [y/N] ").strip().lower() == "y"
        rom_src   = None
        do_clean  = False
        if do_import:
            path = input("  ROM source folder: ").strip().strip('"')
            rom_src = Path(path)
            if not rom_src.is_dir():
                _warn(f"Folder not found — skipping ROM import.")
                rom_src   = None
                do_import = False
            else:
                do_clean = input("  Analyze possible duplicate/bad-dump variants? [y/N] ").strip().lower() == "y"

        print()
        _head("── Phase 1: Installing PocketOS ──")
        try:
            install(self._sd, _log)
        except Exception as e:
            _err(f"Install failed: {e}")
            return

        roms_root = self._sd / "Roms"
        affected_systems: set = set()

        if rom_src and rom_src.is_dir():
            _head("── Phase 2: Importing ROMs ──")
            zips = sorted(rom_src.glob("*.zip"))
            _info(f"Found {len(zips)} ZIP(s) in {rom_src}")
            extracted_total = skipped = 0
            for zip_path in zips:
                ext, candidates = detect_system(zip_path)
                if not candidates:
                    _info(f"[?] {zip_path.name} — unrecognised, skipping")
                    skipped += 1
                    continue
                dest_folder = find_system_folder(roms_root, candidates)
                if dest_folder is None:
                    _warn(f"[{candidates[0]}] {zip_path.name} — Onion system folder is not installed; skipping")
                    skipped += 1
                    continue
                _info(f"[{dest_folder.name}] {zip_path.name}")
                new_files = extract_zip(zip_path, dest_folder, {ext}, _log)
                extracted_total += len(new_files)
                if new_files:
                    affected_systems.add(dest_folder.name)
            _ok(f"Extracted {extracted_total} file(s), {skipped} unrecognised skipped")

            if do_clean and affected_systems:
                _head("── Phase 2b: Analyzing possible variants (no files deleted) ──")
                total_flagged = 0
                for sys_folder in sorted(affected_systems):
                    flagged = clean_variants(roms_root / sys_folder, _log)
                    if flagged:
                        _ok(f"{sys_folder}: flagged {flagged} possible variant(s)")
                        total_flagged += flagged
                _ok(f"Total flagged: {total_flagged}; no ROM files were changed")
        else:
            _info("Phase 2: ROM import skipped")
            if roms_root.is_dir():
                for d in roms_root.iterdir():
                    if d.is_dir():
                        has_roms = any(f.suffix.lower() in ROM_EXTS
                                       for f in d.iterdir() if f.is_file())
                        if has_roms and not (d / "miyoogamelist.xml").exists():
                            affected_systems.add(d.name)

        db_path   = _find_db()
        overrides = _load_overrides()
        if not affected_systems:
            _info("Phase 3: Genre scan skipped (no new ROMs)")
        elif not db_path:
            _info("Phase 3: Genre scan skipped (openvgdb.sqlite not found)")
        else:
            _head("── Phase 3: Scanning genres ──")
            total_added = 0
            for sys_folder in sorted(affected_systems):
                added = scan_genres_for_system(roms_root, sys_folder, db_path, _log)
                if added:
                    _ok(f"{sys_folder}: added {added} genre entry/entries")
                    total_added += added
            _ok(f"Total genre entries added: {total_added}")

            if overrides:
                _head("── Phase 3b: Applying manual genre overrides ──")
                total_fixed = 0
                for sys_folder in sorted(affected_systems):
                    fixed = apply_overrides(roms_root, sys_folder, overrides, _log)
                    if fixed:
                        _ok(f"{sys_folder}: fixed {fixed} override(s)")
                        total_fixed += fixed
                _ok(f"Total overrides applied: {total_fixed}")

        print()
        _ok(f"{_BOLD}Setup complete!{_R}")
        _info("Eject your SD card safely, insert into your Miyoo Mini Plus, and power on.")
        _info("PocketOS launches automatically.")
        print()

    # ── Uninstall ──

    def _do_uninstall(self):
        print()
        _warn("This will remove PocketOS from your SD card.")
        _info("Your games, saves, and settings are not affected.")
        print()
        confirm = input("  Type YES to confirm: ").strip()
        if confirm != "YES":
            _info("Cancelled.")
            return
        print()
        try:
            uninstall(self._sd, _log)
        except Exception as e:
            _err(f"Uninstall failed: {e}")
            return
        print()
        _ok("PocketOS removed. Eject your SD card safely.")
        print()


def _ensure_terminal():
    """If not running in a terminal (e.g. double-clicked), relaunch inside one."""
    if sys.stdin.isatty():
        return
    import subprocess
    import shlex
    exe = os.path.abspath(sys.argv[0])
    quoted = shlex.quote(exe)
    # bash -c wrapper keeps the window open after exit so the user can read output
    bash_cmd = f"{quoted}; echo; read -p 'Press Enter to close...'"
    attempts = [
        ["gnome-terminal", "--", "bash", "-c", bash_cmd],
        ["x-terminal-emulator", "-e", f"bash -c {shlex.quote(bash_cmd)}"],
        ["xfce4-terminal", "-e", f"bash -c {shlex.quote(bash_cmd)}"],
        ["konsole", "-e", "bash", "-c", bash_cmd],
        ["xterm", "-e", "bash", "-c", bash_cmd],
    ]
    for cmd in attempts:
        try:
            subprocess.Popen(cmd)
            sys.exit(0)
        except FileNotFoundError:
            continue


if __name__ == "__main__":
    _ensure_terminal()
    Installer().run()
