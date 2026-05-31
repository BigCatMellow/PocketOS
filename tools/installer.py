#!/usr/bin/env python3
"""
PocketOS Setup Suite
Installs PocketOS, imports your ROMs, cleans up duplicates,
and sets up Browse by Genre — all in one run.
"""

import os
import re
import sys
import json
import zlib
import shutil
import sqlite3
import threading
import subprocess
import tempfile
import urllib.request
import urllib.error
import webbrowser
import zipfile
import xml.etree.ElementTree as ET
from xml.dom import minidom
from pathlib import Path
import tkinter as tk
from tkinter import ttk, filedialog, messagebox

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

def _load_overrides() -> dict:
    fix_path = Path(__file__).parent / "fix_unsorted.py"
    if not fix_path.exists():
        return {}
    ns = {}
    exec(fix_path.read_text(), ns)
    return ns.get("OVERRIDES", {})

def _find_db() -> Path | None:
    p = Path(__file__).parent / "openvgdb.sqlite"
    return p if p.exists() else None


# ── pywebview app ─────────────────────────────────────────────────────────────

import webview as _webview
import json as _json


class Api:
    def __init__(self):
        self._window = None

    def set_window(self, w):
        self._window = w

    # ── JS bridge ──

    def _js(self, expr):
        try:
            if self._window:
                self._window.evaluate_js(expr)
        except Exception:
            pass

    def _push_log(self, text, kind="info"):
        self._js(f'window.__pushLog({_json.dumps(str(text))},"{kind}")')

    def _push_pct(self, pct):
        self._js(f'window.__pushPct({int(pct)})')

    def _push_phase(self, phase):
        self._js(f'window.__setPhase("{phase}")')

    def _log(self, text):
        t = text.strip()
        kind = ("ok"   if t.startswith("✓") else
                "err"  if t.startswith("✗") or "ERROR" in text else
                "done" if "complete" in text.lower() or "setup complete" in text.lower() else
                "info")
        self._push_log(text, kind)

    # ── API methods called from JS ──

    def auto_detect(self):
        candidates = []
        if sys.platform == "win32":
            import string
            for letter in string.ascii_uppercase:
                p = Path(f"{letter}:\\")
                if p.exists() and detect_sd(p):
                    candidates.append({"path": str(p), "name": f"{letter}:\\"})
        else:
            for mount in [Path("/media"), Path("/mnt"), Path("/Volumes")]:
                if not mount.exists():
                    continue
                try:
                    for child in mount.iterdir():
                        if detect_sd(child):
                            candidates.append({"path": str(child), "name": child.name})
                        for gc in (child.iterdir() if child.is_dir() else []):
                            if detect_sd(gc):
                                candidates.append({"path": str(gc), "name": gc.name})
                except PermissionError:
                    pass
        return candidates

    def validate_path(self, path):
        p = Path(path)
        if not p.is_dir():
            return {"valid": False, "onion": False, "name": ""}
        return {
            "valid": detect_sd(p),
            "onion": detect_onion(p),
            "name":  p.name or str(p),
        }

    def browse_sd(self):
        result = self._window.create_file_dialog(_webview.FOLDER_DIALOG)
        return result[0] if result else None

    def browse_roms(self):
        result = self._window.create_file_dialog(_webview.FOLDER_DIALOG)
        return result[0] if result else None

    def start_install(self, sd_path, rom_src, do_import, do_clean):
        if not PAYLOAD_BIN.exists():
            self._push_log(
                f"✗ PocketOS payload not found: {PAYLOAD_BIN}\n"
                "Try re-downloading from the releases page.", "err")
            self._push_phase("error")
            return

        def _run():
            try:
                self._run_setup(
                    Path(sd_path),
                    Path(rom_src) if do_import and rom_src else None,
                    do_clean)
            except Exception as e:
                self._push_log(f"✗ ERROR: {e}", "err")
                self._push_phase("error")

        threading.Thread(target=_run, daemon=True).start()

    def _run_setup(self, sd, rom_src, do_clean):
        roms_root = sd / "Roms"

        self._push_log("── Phase 1: Installing PocketOS ──")
        self._push_pct(10)
        install(sd, self._log)
        self._push_log("✓ PocketOS installed\n", "ok")
        self._push_pct(35)

        affected_systems: set = set()
        if rom_src and rom_src.is_dir():
            self._push_log("── Phase 2: Importing ROMs ──")
            zips = sorted(rom_src.glob("*.zip"))
            self._push_log(f"  Found {len(zips)} ZIP(s) in {rom_src}")
            extracted_total = skipped = 0
            for i, zip_path in enumerate(zips):
                ext, candidates = detect_system(zip_path)
                if not candidates:
                    self._push_log(f"  [?] {zip_path.name} — unrecognised, skipping")
                    skipped += 1
                    continue
                dest_folder = find_system_folder(roms_root, candidates)
                if dest_folder is None:
                    dest_folder = roms_root / candidates[0]
                    dest_folder.mkdir(parents=True, exist_ok=True)
                self._push_log(f"  [{dest_folder.name}] {zip_path.name}")
                new_files = extract_zip(zip_path, dest_folder, self._log)
                extracted_total += len(new_files)
                if new_files:
                    affected_systems.add(dest_folder.name)
                self._push_pct(35 + int(35 * (i + 1) / max(len(zips), 1)))
            self._push_log(f"  Extracted {extracted_total} file(s), {skipped} unrecognised skipped")

            if do_clean and affected_systems:
                self._push_log("── Phase 2b: Removing duplicate/bad dumps ──")
                total_removed = 0
                for sys_folder in sorted(affected_systems):
                    removed = clean_variants(roms_root / sys_folder, self._log)
                    if removed:
                        self._push_log(f"  {sys_folder}: removed {removed} variant(s)")
                        total_removed += removed
                self._push_log(f"  Total removed: {total_removed}")
        else:
            self._push_log("── Phase 2: ROM import skipped ──")
            if roms_root.is_dir():
                for d in roms_root.iterdir():
                    if d.is_dir():
                        has_roms = any(
                            f.suffix.lower() in ROM_EXTS
                            for f in d.iterdir() if f.is_file())
                        if has_roms and not (d / "miyoogamelist.xml").exists():
                            affected_systems.add(d.name)

        self._push_pct(70)

        db_path   = _find_db()
        overrides = _load_overrides()
        if not affected_systems:
            self._push_log("── Phase 3: Genre scan skipped (no new ROMs) ──")
        elif not db_path:
            self._push_log("── Phase 3: Genre scan skipped (openvgdb.sqlite not found) ──")
        else:
            self._push_log("── Phase 3: Scanning genres ──")
            total_added = 0
            for i, sys_folder in enumerate(sorted(affected_systems)):
                added = scan_genres_for_system(roms_root, sys_folder, db_path, self._log)
                if added:
                    self._push_log(f"  {sys_folder}: added {added} genre entry/entries")
                    total_added += added
                self._push_pct(70 + int(25 * (i + 1) / max(len(affected_systems), 1)))
            self._push_log(f"  Total genre entries added: {total_added}")

            if overrides:
                self._push_log("── Phase 3b: Applying manual genre overrides ──")
                total_fixed = 0
                for sys_folder in sorted(affected_systems):
                    fixed = apply_overrides(roms_root, sys_folder, overrides, self._log)
                    if fixed:
                        self._push_log(f"  {sys_folder}: fixed {fixed} override(s)")
                        total_fixed += fixed
                self._push_log(f"  Total overrides applied: {total_fixed}")

        self._push_pct(99)
        self._push_log("\n✓ Setup complete.", "done")
        self._push_log("  Eject your SD card safely, insert into your Miyoo Mini Plus, and power on.")
        self._push_log("  PocketOS launches automatically.")
        self._push_pct(100)
        self._push_phase("success")

    def start_uninstall(self, sd_path):
        def _run():
            try:
                self._push_pct(30)
                uninstall(Path(sd_path), self._log)
                self._push_pct(80)
                self._push_log("✓ PocketOS removed. Eject your SD card safely.", "done")
                self._push_pct(100)
                self._push_phase("removed")
            except Exception as e:
                self._push_log(f"✗ ERROR: {e}", "err")
                self._push_phase("error")

        threading.Thread(target=_run, daemon=True).start()

    def check_update(self):
        def _run():
            tag, url = fetch_latest_release()
            if not tag or not url:
                return
            try:
                if version_tuple(tag) > version_tuple(VERSION):
                    self._js(f'window.__showUpdate({_json.dumps(tag)},{_json.dumps(url)})')
            except Exception:
                pass

        threading.Thread(target=_run, daemon=True).start()

    def download_update(self, tag, url):
        def _run():
            tmp_dir = None
            try:
                self._push_log(f"► Downloading PocketOS {tag} from GitHub…")
                tmp_dir = tempfile.mkdtemp(prefix="pocketos_")
                zip_path = Path(tmp_dir) / f"pocketOS-{tag}.zip"

                def _progress(count, block, total):
                    if total > 0:
                        self._push_pct(min(90, count * block * 100 // total))

                urllib.request.urlretrieve(url, zip_path, reporthook=_progress)
                self._push_log("► Extracting…")
                extract_dir = Path(tmp_dir) / "extracted"
                with zipfile.ZipFile(zip_path) as zf:
                    zf.extractall(extract_dir)
                src_dir = extract_dir
                if not (src_dir / ".tmp_update").is_dir():
                    for child in extract_dir.iterdir():
                        if child.is_dir() and (child / ".tmp_update").is_dir():
                            src_dir = child
                            break
                sd_path = self._window.evaluate_js("window.__getSdPath()")
                if not sd_path:
                    raise RuntimeError("No SD card selected")
                self._push_log(f"► Installing PocketOS {tag}…")
                install_from_dir(src_dir, Path(sd_path), self._log)
                self._push_log(f"✓ PocketOS {tag} installed!", "done")
                self._push_pct(100)
                self._push_phase("success")
                self._js("window.__showUpdate(null,null)")
            except Exception as e:
                self._push_log(f"✗ ERROR: {e}", "err")
                self._push_phase("error")
            finally:
                if tmp_dir:
                    shutil.rmtree(tmp_dir, ignore_errors=True)

        threading.Thread(target=_run, daemon=True).start()

    def open_url(self, url):
        webbrowser.open(url)


def main():
    if getattr(sys, "frozen", False):
        ui_dir = Path(sys._MEIPASS) / "ui"
    else:
        ui_dir = Path(__file__).parent / "ui"

    api = Api()
    window = _webview.create_window(
        "PocketOS Installer",
        url=(ui_dir / "index.html").as_uri(),
        js_api=api,
        width=580,
        height=880,
        resizable=False,
        background_color="#121929",
    )
    api.set_window(window)
    _webview.start()


if __name__ == "__main__":
    main()

