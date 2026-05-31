#!/usr/bin/env python3
"""PocketOS Installer — terminal edition"""

import os
import re
import sys
import json
import zlib
import shutil
import sqlite3
import tempfile
import urllib.request
import urllib.error
import webbrowser
import zipfile
import xml.etree.ElementTree as ET
from xml.dom import minidom
from pathlib import Path

# ── Bundled assets path ───────────────────────────────────────────────────────
if getattr(sys, "frozen", False):
    BASE_DIR = Path(sys._MEIPASS)
else:
    BASE_DIR = Path(__file__).parent.parent / "release" / "pocketOS-v1.0"

PAYLOAD_BIN = BASE_DIR / ".tmp_update" / "bin" / "pocketOS"
PAYLOAD_RES = BASE_DIR / ".tmp_update" / "res" / "pocketos"

ONION_URL   = "https://github.com/OnionUI/Onion/releases/latest"
GITHUB_API  = "https://api.github.com/repos/BigCatMellow/PocketOS/releases/latest"
GITHUB_REPO = "https://github.com/BigCatMellow/PocketOS/releases/latest"

VERSION = "v1.0"


# ── ROM import constants ──────────────────────────────────────────────────────

EXT_TO_SYSTEMS = {
    ".nes":  ["FC",  "NES"],      ".fds":  ["FC",  "NES"],
    ".sfc":  ["SFC", "SNES"],     ".smc":  ["SFC", "SNES"],
    ".gb":   ["GB",  "SGB"],      ".gbc":  ["GBC"],
    ".gba":  ["GBA"],             ".n64":  ["N64"],
    ".z64":  ["N64"],             ".v64":  ["N64"],
    ".nds":  ["NDS"],             ".md":   ["MD",  "GEN", "GENESIS"],
    ".smd":  ["MD",  "GEN", "GENESIS"],
    ".gen":  ["MD",  "GEN", "GENESIS"],
    ".sms":  ["SMS"],             ".gg":   ["GG"],
    ".pce":  ["PCE"],             ".lnx":  ["LYNX"],
    ".ws":   ["WSWAN"],           ".wsc":  ["WSWANC"],
    ".ngp":  ["NGP"],             ".ngc":  ["NGPC"],
    ".col":  ["COLECO"],          ".iso":  ["PS"],
    ".bin":  ["PS"],              ".cue":  ["PS"],
    ".pbp":  ["PS"],              ".chd":  ["PS"],
    ".img":  ["PS"],
}

ROM_EXTS = set(EXT_TO_SYSTEMS.keys())

DOC_NAMES = {"readme", "license", "changelog", "credits", "notes", "info", "manual"}

SYSTEM_MAP = {
    "FC": "Nintendo Entertainment System",       "NES": "Nintendo Entertainment System",
    "SFC": "Nintendo Super Nintendo Entertainment System",
    "SNES": "Nintendo Super Nintendo Entertainment System",
    "GB": "Nintendo Game Boy",                   "SGB": "Nintendo Game Boy",
    "GBC": "Nintendo Game Boy Color",            "GBA": "Nintendo Game Boy Advance",
    "N64": "Nintendo 64",                        "NDS": "Nintendo DS",
    "MD": "Sega Genesis/Mega Drive",             "GEN": "Sega Genesis/Mega Drive",
    "GENESIS": "Sega Genesis/Mega Drive",        "SMS": "Sega Master System",
    "GG": "Sega Game Gear",                      "PCE": "NEC PC Engine/TurboGrafx-16",
    "LYNX": "Atari Lynx",                        "WSWAN": "Bandai WonderSwan",
    "WSWANC": "Bandai WonderSwan Color",         "NGP": "SNK Neo Geo Pocket",
    "NGPC": "SNK Neo Geo Pocket Color",          "COLECO": "Coleco ColecoVision",
    "PS": "Sony PlayStation",
}

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
           (path / ".tmp_update" / "onion_version").exists() or \
           (path / "BIOS").is_dir()

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

def install_from_dir(src: Path, sd: Path, log):
    bin_src  = src / ".tmp_update" / "bin" / "pocketOS"
    res_src  = src / ".tmp_update" / "res" / "pocketos"
    bin_dest = sd  / ".tmp_update" / "bin"
    res_dest = sd  / ".tmp_update" / "res" / "pocketos"
    if not bin_src.exists():
        raise FileNotFoundError(f"Binary not found: {bin_src}")
    log("  Setting up folders on SD card...")
    bin_dest.mkdir(parents=True, exist_ok=True)
    res_dest.mkdir(parents=True, exist_ok=True)
    log("  Copying PocketOS launcher...")
    shutil.copy2(bin_src, bin_dest / "pocketOS")
    log("  Copying themes, icons, and fonts...")
    if res_dest.exists():
        shutil.rmtree(res_dest)
    shutil.copytree(res_src, res_dest)

def install(sd: Path, log):
    install_from_dir(BASE_DIR, sd, log)

def uninstall(sd: Path, log):
    log("  Removing PocketOS launcher...")
    target = sd / ".tmp_update" / "bin" / "pocketOS"
    if target.exists():
        target.unlink()
        log("  Removed launcher binary")
    else:
        log("  PocketOS binary not found — may already be uninstalled")
    log("  Removing themes and assets...")
    res = sd / ".tmp_update" / "res" / "pocketos"
    if res.exists():
        shutil.rmtree(res)
        log("  Removed themes, icons, and fonts")


# ── ROM import helpers ────────────────────────────────────────────────────────

def _is_doc_file(name: str) -> bool:
    stem = Path(name).stem.lower()
    return stem in DOC_NAMES or any(stem.startswith(d) for d in DOC_NAMES)

def detect_system(zip_path: Path):
    AMBIGUOUS = {".bin", ".img", ".iso", ".chd"}
    try:
        with zipfile.ZipFile(zip_path) as zf:
            names = [n for n in zf.namelist() if not n.endswith("/")]
            best_ext, best_candidates = None, []
            for name in names:
                if _is_doc_file(name):
                    continue
                ext = Path(name).suffix.lower()
                if ext not in EXT_TO_SYSTEMS:
                    continue
                candidates = EXT_TO_SYSTEMS[ext]
                if ext not in AMBIGUOUS:
                    return ext, candidates
                if not best_ext:
                    best_ext, best_candidates = ext, candidates
            return best_ext, best_candidates
    except Exception:
        pass
    return None, []

def find_system_folder(roms_root: Path, candidates: list):
    for name in candidates:
        p = roms_root / name
        if p.is_dir():
            return p
    return None

def extract_zip(zip_path: Path, dest_folder: Path, log) -> list:
    extracted = []
    try:
        with zipfile.ZipFile(zip_path) as zf:
            for member in [n for n in zf.namelist() if not n.endswith("/")]:
                ext = Path(member).suffix.lower()
                if ext not in ROM_EXTS or _is_doc_file(member):
                    continue
                out_path = dest_folder / Path(member).name
                if out_path.exists():
                    log(f"    SKIP (exists): {out_path.name}")
                    continue
                out_path.write_bytes(zf.read(member))
                extracted.append(out_path)
                log(f"    extracted: {out_path.name}")
    except Exception as e:
        log(f"    ERROR reading {zip_path.name}: {e}")
    return extracted


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
    rom_files = [f for f in sorted(folder.iterdir())
                 if f.is_file() and f.suffix.lower() in ROM_EXTS]
    groups: dict = {}
    for f in rom_files:
        groups.setdefault(_base_name(f.stem), []).append(f)
    removed = 0
    for files in groups.values():
        if len(files) == 1:
            continue
        scored = sorted(files, key=lambda f: (-_rom_score(f.name), f.name))
        log(f"    keep:   {scored[0].name}")
        for f in scored[1:]:
            log(f"    remove: {f.name}")
            f.unlink()
            removed += 1
    return removed


# ── Genre scan helpers ────────────────────────────────────────────────────────

def _crc32_of(path: Path) -> str:
    try:
        with open(path, "rb") as f:
            data = f.read(64 * 1024 * 1024)
        return f"{zlib.crc32(data) & 0xFFFFFFFF:08X}"
    except Exception:
        return ""

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

def _load_existing(gamelist: Path) -> dict:
    result = {}
    if not gamelist.exists():
        return result
    try:
        for el in ET.parse(gamelist).getroot().findall("game"):
            path = (el.findtext("path") or "").lstrip("./")
            result[path] = {"path": path,
                            "name":  el.findtext("name") or path,
                            "genre": el.findtext("genre") or "Unsorted"}
    except Exception:
        pass
    return result

def _write_gamelist(games: list, dest: Path):
    root = ET.Element("gameList")
    for g in sorted(games, key=lambda x: x["name"].lower()):
        el = ET.SubElement(root, "game")
        ET.SubElement(el, "path").text  = "./" + g["path"]
        ET.SubElement(el, "name").text  = g["name"]
        ET.SubElement(el, "genre").text = g["genre"]
    pretty = minidom.parseString(ET.tostring(root, encoding="unicode")).toprettyxml(indent="  ", encoding=None)
    dest.write_text(pretty, encoding="utf-8")

def scan_genres_for_system(roms_root: Path, system_folder: str, db_path: Path, log) -> int:
    system_dir  = roms_root / system_folder
    gamelist    = system_dir / "miyoogamelist.xml"
    system_name = SYSTEM_MAP.get(system_folder.upper())
    if not system_name:
        return 0
    try:
        db = sqlite3.connect(str(db_path))
    except Exception as e:
        log(f"    DB open failed: {e}")
        return 0
    existing = _load_existing(gamelist)
    games    = list(existing.values())
    added    = 0
    for rom in sorted(system_dir.iterdir()):
        if rom.suffix.lower() in {".xml", ".db", ".txt", ""} or not rom.is_file():
            continue
        if rom.name in existing:
            continue
        result = _db_lookup(db, rom, system_name)
        name, genre = result if result else (rom.stem, "Unsorted")
        games.append({"path": rom.name, "name": name, "genre": genre})
        added += 1
    db.close()
    if added:
        _write_gamelist(games, gamelist)
    return added

def apply_overrides(roms_root: Path, system_folder: str, overrides: dict, log) -> int:
    gamelist = roms_root / system_folder / "miyoogamelist.xml"
    if not gamelist.exists():
        return 0
    try:
        tree = ET.parse(gamelist)
    except Exception:
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
        pretty = minidom.parseString(ET.tostring(root, encoding="unicode")).toprettyxml(indent="  ", encoding=None)
        gamelist.write_text(pretty, encoding="utf-8")
    return changed

def _asset_dir() -> Path:
    """Directory to search for loose data files (openvgdb, overrides).
    When frozen: folder containing the executable.
    When running from source: the tools/ directory."""
    if getattr(sys, "frozen", False):
        return Path(sys.executable).parent
    return Path(__file__).parent

def _load_overrides() -> dict:
    fix_path = _asset_dir() / "fix_unsorted.py"
    if not fix_path.exists():
        return {}
    ns = {}
    exec(fix_path.read_text(), ns)
    return ns.get("OVERRIDES", {})

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
            choice = input(f"\n  Select [1-{len(candidates)}]: ").strip()
            try:
                self._sd = candidates[int(choice) - 1]
            except (ValueError, IndexError):
                self._sd = candidates[0]
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
        if not PAYLOAD_BIN.exists():
            _err(f"Payload not found: {PAYLOAD_BIN}")
            _info("Re-download the installer from the releases page.")
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
                do_clean = input("  Remove duplicate/bad dumps? [Y/n] ").strip().lower() != "n"

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
                    dest_folder = roms_root / candidates[0]
                    dest_folder.mkdir(parents=True, exist_ok=True)
                _info(f"[{dest_folder.name}] {zip_path.name}")
                new_files = extract_zip(zip_path, dest_folder, _log)
                extracted_total += len(new_files)
                if new_files:
                    affected_systems.add(dest_folder.name)
            _ok(f"Extracted {extracted_total} file(s), {skipped} unrecognised skipped")

            if do_clean and affected_systems:
                _head("── Phase 2b: Removing duplicate/bad dumps ──")
                total_removed = 0
                for sys_folder in sorted(affected_systems):
                    removed = clean_variants(roms_root / sys_folder, _log)
                    if removed:
                        _ok(f"{sys_folder}: removed {removed} variant(s)")
                        total_removed += removed
                _ok(f"Total removed: {total_removed}")
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


if __name__ == "__main__":
    Installer().run()
