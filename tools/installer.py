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
        url=str(ui_dir / "index.html"),
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


# ── legacy tkinter stubs (kept so old imports don't break) ────────────────────

def _bordered(parent, bg, border_color, bd=2, **kw):
    """Frame with a flat color border (using highlightthickness)."""
    return tk.Frame(parent, bg=bg,
                    highlightthickness=bd, highlightbackground=border_color, **kw)

def _pill_label(parent, text, bg, fg, font):
    """Small rounded-look label (just a Label with padding — closest tkinter gets)."""
    return tk.Label(parent, text=text, bg=bg, fg=fg, font=font, padx=7, pady=2)


class App(tk.Tk):
    # ── Design tokens (from index.html) ────────────────────────────────────────
    NAVY      = "#0d1a2e"
    NAVY2     = "#13243f"
    CREAM     = "#efe7d8"
    CREAM_ROW = "#f6f1e7"
    CREAM_IN  = "#e7decb"
    INK       = "#16233f"
    INK_SOFT  = "#6c7591"
    LAV       = "#d4ccf6"
    LAV_SOFT  = "#e4dffa"
    LAV_BRD   = "#9a89dc"
    LAV_DEEP  = "#7b69cf"
    LAV_SHD   = "#5d4caf"
    LIME      = "#82ea5f"
    LIME_DP   = "#4fb733"
    RED       = "#ea5440"
    RED_DP    = "#b6392a"
    AMBER     = "#f0b43c"

    SEG_COUNT = 22

    def __init__(self):
        super().__init__()
        self.title("PocketOS Installer")
        self.resizable(False, False)
        self._sd_path   = tk.StringVar()
        self._rom_src   = tk.StringVar()
        self._import_on = tk.BooleanVar(value=False)
        self._clean_on  = tk.BooleanVar(value=True)
        self._latest_tag = None
        self._latest_url = None
        self._phase = "idle"   # idle|ready|installing|success|confirm|uninstalling|removed|error
        self._pct   = 0
        self._build_ui()
        self._center()
        self._auto_detect()
        self._check_for_update()

    def _center(self):
        self.update_idletasks()
        w, h = self.winfo_width(), self.winfo_height()
        sw, sh = self.winfo_screenwidth(), self.winfo_screenheight()
        self.geometry(f"+{(sw - w) // 2}+{(sh - h) // 2}")

    # ─────────────────────────────────────────────────────────────────────────
    # UI construction
    # ─────────────────────────────────────────────────────────────────────────

    def _build_ui(self):
        self.configure(bg=self.CREAM)

        self._build_titlebar()
        self._build_update_banner()
        self._build_hero()
        self._build_content()
        self._build_footer()

        self._sd_path.trace_add("write", lambda *_: self._validate())
        self._toggle_import()
        self._set_phase("idle")

    # ── Title bar ─────────────────────────────────────────────────────────────

    def _build_titlebar(self):
        bar = tk.Frame(self, bg=self.NAVY, height=40)
        bar.pack(fill="x")
        bar.pack_propagate(False)

        # Logo glyph
        tk.Label(bar, text="◈", fg=self.LAV, bg=self.NAVY,
                 font=("Helvetica", 13)).pack(side="left", padx=(10, 4))

        tk.Label(bar, text="PocketOS Installer", fg="#eaf1ff", bg=self.NAVY,
                 font=("Helvetica", 11, "bold")).pack(side="left")
        tk.Label(bar, text="  " + VERSION, fg="#7e8db0", bg=self.NAVY,
                 font=("Courier", 12)).pack(side="left")

        # Close / reset button
        tk.Button(bar, text="✕", bg="#b8382c", fg="white",
                  activebackground="#e1483a", activeforeground="white",
                  font=("Helvetica", 11, "bold"), relief="flat", bd=0,
                  padx=8, cursor="hand2", command=self._reset
                  ).pack(side="right", padx=(0, 6), pady=6)

        # Status pill (hidden until card selected)
        self._pill_var = tk.StringVar()
        self._pill_lbl = tk.Label(bar, textvariable=self._pill_var,
                                   bg=self.NAVY2, fg=self.LIME,
                                   font=("Courier", 12, "bold"), padx=9, pady=3,
                                   relief="flat")
        self._pill_lbl.pack(side="right", padx=(0, 8), pady=8)
        self._pill_lbl.pack_forget()

    def _set_pill(self, text: str, fg: str):
        if text:
            self._pill_var.set("  " + text + "  ")
            self._pill_lbl.config(fg=fg)
            self._pill_lbl.pack(side="right", padx=(0, 8), pady=8)
        else:
            self._pill_lbl.pack_forget()

    # ── Update banner ─────────────────────────────────────────────────────────

    def _build_update_banner(self):
        self._update_frame = tk.Frame(self, bg="#1e3a5f", padx=16, pady=7)
        self._update_lbl   = tk.Label(self._update_frame, text="", fg="#89dceb",
                                       bg="#1e3a5f", font=("Helvetica", 9), anchor="w")
        self._update_lbl.pack(side="left", fill="x", expand=True)
        self._update_btn = tk.Button(self._update_frame, text="",
                                      command=self._do_update_install,
                                      bg="#2a3e5a", fg="#a6e3a1", relief="flat",
                                      padx=10, cursor="hand2",
                                      font=("Helvetica", 9, "bold"))
        self._update_btn.pack(side="right")
        self._update_frame.pack_forget()

    # ── Hero ──────────────────────────────────────────────────────────────────

    def _build_hero(self):
        hero = tk.Frame(self, bg=self.CREAM)
        hero.pack(fill="x", pady=(20, 10))

        wm = tk.Frame(hero, bg=self.CREAM)
        wm.pack()
        tk.Label(wm, text="Pocket", fg=self.INK, bg=self.CREAM,
                 font=("Helvetica", 40, "bold")).pack(side="left")
        tk.Label(wm, text="OS", fg=self.LAV_DEEP, bg=self.CREAM,
                 font=("Helvetica", 40, "bold")).pack(side="left")

        tk.Label(hero, text=f"Installer  ·  {VERSION}",
                 fg=self.INK_SOFT, bg=self.CREAM,
                 font=("Helvetica", 11, "bold")).pack(pady=(2, 0))
        tk.Label(hero,
                 text="A minimal launcher for the Miyoo Mini Plus  ·  Built on Onion OS",
                 fg=self.INK_SOFT, bg=self.CREAM, font=("Helvetica", 9)).pack(pady=(3, 0))

    # ── Content ───────────────────────────────────────────────────────────────

    def _build_content(self):
        PAD = 22
        self._content = tk.Frame(self, bg=self.CREAM, padx=PAD)
        self._content.pack(fill="x")

        self._build_sd_section()
        self._build_onion_warning()
        self._build_rom_import()
        self._build_actions()
        self._build_progress()
        self._build_console()
        tk.Frame(self._content, height=14, bg=self.CREAM).pack()

    def _field_label(self, parent, text, hint=""):
        row = tk.Frame(parent, bg=self.CREAM)
        row.pack(fill="x", pady=(10, 6))
        tk.Label(row, text=text, fg=self.INK, bg=self.CREAM,
                 font=("Helvetica", 10, "bold")).pack(side="left")
        if hint:
            tk.Label(row, text="  " + hint, fg=self.INK_SOFT, bg=self.CREAM,
                     font=("Helvetica", 9)).pack(side="left")

    def _build_sd_section(self):
        self._field_label(self._content, "SD CARD",
                          "select the root of your Miyoo SD card")

        sd_row = tk.Frame(self._content, bg=self.CREAM)
        sd_row.pack(fill="x")

        # Path display (inset field)
        sd_field = _bordered(sd_row, self.CREAM_IN, "#c8bfaa", bd=2)
        sd_field.pack(side="left", fill="x", expand=True, ipady=8)
        self._sd_path_lbl = tk.Label(sd_field, textvariable=self._sd_path,
                                      fg=self.INK_SOFT, bg=self.CREAM_IN,
                                      font=("Courier", 13), anchor="w",
                                      padx=10, pady=2)
        self._sd_path_lbl.pack(fill="x")

        tk.Button(sd_row, text="Browse…", command=self._browse_sd,
                  bg=self.CREAM_ROW, fg=self.INK, activebackground=self.LAV_SOFT,
                  font=("Helvetica", 10, "bold"), relief="flat", bd=0,
                  padx=14, pady=10, cursor="hand2",
                  highlightthickness=2, highlightbackground="#c8bfaa"
                  ).pack(side="left", padx=(8, 0))

        # Detect label
        self._detect_lbl = tk.Label(self._content, text="", fg=self.INK_SOFT,
                                     bg=self.CREAM, font=("Helvetica", 9))
        self._detect_lbl.pack(anchor="w", pady=(4, 0))

        # Card chip (hidden until SD detected)
        self._card_chip = _bordered(self._content, self.LAV_SOFT, self.LAV_BRD, bd=2,
                                     padx=12, pady=8)
        chip_inner = tk.Frame(self._card_chip, bg=self.LAV_SOFT)
        chip_inner.pack(fill="x")
        chip_left = tk.Frame(chip_inner, bg=self.LAV_SOFT)
        chip_left.pack(side="left", fill="x", expand=True)
        self._chip_name = tk.Label(chip_left, text="", fg=self.INK, bg=self.LAV_SOFT,
                                    font=("Helvetica", 11, "bold"), anchor="w")
        self._chip_name.pack(fill="x")
        self._chip_det = tk.Label(chip_left, text="", fg=self.INK_SOFT, bg=self.LAV_SOFT,
                                   font=("Helvetica", 9), anchor="w")
        self._chip_det.pack(fill="x")
        tk.Label(chip_inner, text="✓  DETECTED", fg=self.LIME_DP, bg=self.LAV_SOFT,
                 font=("Courier", 12, "bold"), padx=8).pack(side="right")

    def _build_onion_warning(self):
        self._onion_frame = tk.Frame(self, bg="#2a1f0f", padx=22, pady=8)
        self._onion_lbl   = tk.Label(self._onion_frame, text="", fg="#fab387",
                                      bg="#2a1f0f", font=("Helvetica", 9),
                                      justify="left", anchor="w", wraplength=420)
        self._onion_lbl.pack(side="left", fill="x", expand=True)
        tk.Button(self._onion_frame, text="Get Onion OS →",
                  command=lambda: webbrowser.open(ONION_URL),
                  bg="#3a2e1a", fg="#89b4fa", relief="flat", padx=10,
                  cursor="hand2", font=("Helvetica", 9, "bold")
                  ).pack(side="right")
        self._onion_frame.pack_forget()

    def _build_rom_import(self):
        sep = tk.Frame(self._content, height=1, bg=self.CREAM_IN)
        sep.pack(fill="x", pady=(14, 0))

        self._field_label(self._content, "ROM IMPORT",
                          "optional · unzip ROMs, scan genres, clean duplicates")

        chk = tk.Checkbutton(self._content, text="Import ROMs from a folder of ZIP files",
                              variable=self._import_on, command=self._toggle_import,
                              fg=self.INK, bg=self.CREAM,
                              selectcolor=self.CREAM_IN, activebackground=self.CREAM,
                              font=("Helvetica", 10))
        chk.pack(anchor="w")

        # ROM folder row (shown when checkbox ticked)
        self._rom_row = tk.Frame(self._content, bg=self.CREAM)
        rom_field = _bordered(self._rom_row, self.CREAM_IN, "#c8bfaa", bd=2)
        rom_field.pack(side="left", fill="x", expand=True, ipady=8)
        self._rom_path_lbl = tk.Label(rom_field, textvariable=self._rom_src,
                                       fg=self.INK_SOFT, bg=self.CREAM_IN,
                                       font=("Courier", 12), anchor="w", padx=10, pady=2)
        self._rom_path_lbl.pack(fill="x")
        tk.Button(self._rom_row, text="Browse…", command=self._browse_roms,
                  bg=self.CREAM_ROW, fg=self.INK, activebackground=self.LAV_SOFT,
                  font=("Helvetica", 10, "bold"), relief="flat", bd=0,
                  padx=14, pady=10, cursor="hand2",
                  highlightthickness=2, highlightbackground="#c8bfaa"
                  ).pack(side="left", padx=(8, 0))

        self._clean_chk = tk.Checkbutton(
            self._content,
            text="Remove duplicate / bad / hacked dumps — keep the best version of each game",
            variable=self._clean_on,
            fg=self.INK_SOFT, bg=self.CREAM,
            selectcolor=self.CREAM_IN, activebackground=self.CREAM,
            font=("Helvetica", 9))

    def _build_actions(self):
        sep = tk.Frame(self._content, height=1, bg=self.CREAM_IN)
        sep.pack(fill="x", pady=(14, 0))

        self._actions_frame = tk.Frame(self._content, bg=self.CREAM)
        self._actions_frame.pack(fill="x", pady=(12, 0))

        # Primary install button (lavender, matches btn-primary)
        self._install_btn = tk.Button(
            self._actions_frame,
            text="▶  Install PocketOS",
            command=self._on_install_btn,
            bg=self.LAV, fg=self.INK,
            activebackground=self.LAV_SOFT, activeforeground=self.INK,
            font=("Helvetica", 14, "bold"), relief="flat", bd=0,
            padx=16, pady=12, cursor="hand2",
            highlightthickness=2, highlightbackground=self.LAV_BRD,
        )
        self._install_btn.pack(side="left", fill="x", expand=True, padx=(0, 8))

        # Danger uninstall button
        self._remove_btn = tk.Button(
            self._actions_frame,
            text="Uninstall",
            command=self._do_uninstall,
            bg=self.CREAM_ROW, fg=self.RED,
            activebackground="#fce8e5", activeforeground=self.RED_DP,
            font=("Helvetica", 13, "bold"), relief="flat", bd=0,
            padx=16, pady=12, cursor="hand2",
            highlightthickness=2, highlightbackground=self.RED,
        )
        self._remove_btn.pack(side="left")

        # Hint below buttons
        self._action_hint = tk.Label(
            self._content,
            text="Install: sets up PocketOS, imports ROMs (if selected), scans genres.\n"
                 "Uninstall: removes PocketOS — your games and saves are not affected.",
            fg=self.INK_SOFT, bg=self.CREAM, font=("Helvetica", 8),
            justify="left", anchor="w")
        self._action_hint.pack(fill="x", pady=(4, 0))

    def _build_progress(self):
        self._progress_wrap = tk.Frame(self._content, bg=self.CREAM)

        head = tk.Frame(self._progress_wrap, bg=self.CREAM)
        head.pack(fill="x", pady=(14, 5))
        self._prog_lbl = tk.Label(head, text="Installing", fg=self.INK, bg=self.CREAM,
                                   font=("Helvetica", 11, "bold"))
        self._prog_lbl.pack(side="left")
        self._pct_lbl = tk.Label(head, text="0%", fg=self.LAV_DEEP, bg=self.CREAM,
                                  font=("Courier", 17))
        self._pct_lbl.pack(side="right")

        seg_outer = _bordered(self._progress_wrap, self.CREAM_IN, "#c0b8a2", bd=2,
                               padx=4, pady=4)
        seg_outer.pack(fill="x")
        self._segbar = tk.Canvas(seg_outer, height=14, bg=self.CREAM_IN,
                                  highlightthickness=0)
        self._segbar.pack(fill="x")
        self._segbar.bind("<Configure>", lambda e: self._redraw_segbar())

    def _build_console(self):
        tk.Label(self._content, text="Progress log", fg=self.INK_SOFT, bg=self.CREAM,
                 font=("Helvetica", 8), anchor="w").pack(fill="x", pady=(12, 2))

        console_wrap = _bordered(self._content, self.CREAM_IN, "#c8bfaa", bd=2)
        console_wrap.pack(fill="x")

        self._console = tk.Text(
            console_wrap, height=9,
            bg=self.CREAM_IN, fg=self.INK,
            font=("Courier", 11), relief="flat", bd=0,
            state="disabled", wrap="word",
            padx=10, pady=8,
        )
        scrollbar = tk.Scrollbar(console_wrap, command=self._console.yview,
                                  bg=self.CREAM_IN, troughcolor=self.CREAM_IN,
                                  relief="flat", bd=0)
        self._console.configure(yscrollcommand=scrollbar.set)
        self._console.pack(side="left", fill="both", expand=True)
        scrollbar.pack(side="right", fill="y")

        self._console.tag_configure("gt",    foreground=self.INK_SOFT)
        self._console.tag_configure("info",  foreground=self.INK)
        self._console.tag_configure("ok",    foreground=self.LIME_DP)
        self._console.tag_configure("warn",  foreground="#c98a16")
        self._console.tag_configure("err",   foreground=self.RED)
        self._console.tag_configure("done",  foreground=self.LIME_DP)
        self._console.tag_configure("empty", foreground=self.INK_SOFT)

        self._console.config(state="normal")
        self._console.insert("end", "PocketOS Installer ready. Output will appear here.\n", "empty")
        self._console.config(state="disabled")

    def _build_footer(self):
        foot = tk.Frame(self, bg=self.NAVY, height=44)
        foot.pack(fill="x", side="bottom")
        foot.pack_propagate(False)

        # A hint
        a_frame = tk.Frame(foot, bg=self.NAVY)
        a_frame.pack(side="left", padx=(12, 0), fill="y")
        a_frame.pack_configure(anchor="center")
        self._a_glyph = tk.Label(a_frame, text="A", bg=self.LIME_DP, fg=self.NAVY,
                                  font=("Helvetica", 9, "bold"), padx=5, pady=1)
        self._a_glyph.pack(side="left", anchor="center", pady=13)
        self._a_hint = tk.Label(a_frame, text="Install", fg="#c2cee4", bg=self.NAVY,
                                 font=("Helvetica", 9, "bold"))
        self._a_hint.pack(side="left", padx=(5, 0), anchor="center")

        # B hint
        b_frame = tk.Frame(foot, bg=self.NAVY)
        b_frame.pack(side="left", padx=(10, 0), fill="y")
        b_frame.pack_configure(anchor="center")
        tk.Label(b_frame, text="B", bg="#ef6a4f", fg=self.NAVY,
                 font=("Helvetica", 9, "bold"), padx=5, pady=1
                 ).pack(side="left", anchor="center", pady=13)
        self._b_hint = tk.Label(b_frame, text="Back", fg="#c2cee4", bg=self.NAVY,
                                 font=("Helvetica", 9, "bold"))
        self._b_hint.pack(side="left", padx=(5, 0), anchor="center")

        # Status (right side)
        self._foot_status = tk.Label(foot, text="Select your SD card to get started.",
                                      fg="#aebbd3", bg=self.NAVY,
                                      font=("Helvetica", 9, "bold"))
        self._foot_status.pack(side="right", padx=14, anchor="center")

        self._foot_dot = tk.Label(foot, text="●", fg="#7e8db0", bg=self.NAVY,
                                   font=("Helvetica", 11))
        self._foot_dot.pack(side="right", anchor="center")

    # ─────────────────────────────────────────────────────────────────────────
    # Phase state machine
    # ─────────────────────────────────────────────────────────────────────────

    def _set_phase(self, phase: str, pct: int = None):
        self._phase = phase
        if pct is not None:
            self._pct = pct
        self._refresh_phase()

    def _refresh_phase(self):
        phase = self._phase
        pct   = self._pct
        busy  = phase in ("installing", "uninstalling")
        done  = phase in ("success", "removed")
        has_sd = bool(self._sd_path.get().strip())

        # ── Title pill ──
        pill_map = {
            "ready":        ("READY",          self.LIME),
            "success":      ("DONE",           self.LIME),
            "removed":      ("DONE",           self.LIME),
            "error":        ("ERROR",          self.RED),
            "installing":   (f"{pct}%",        self.AMBER),
            "uninstalling": (f"{pct}%",        self.AMBER),
        }
        if phase in pill_map:
            self._set_pill(*pill_map[phase])
        else:
            self._set_pill("", "")

        # ── Footer status ──
        foot_map = {
            "idle":         ("Select your SD card to get started.",               "#aebbd3"),
            "ready":        ("Card ready — click Install to flash PocketOS.", self.LIME_DP),
            "installing":   (f"Installing PocketOS… {pct}%",                 self.AMBER),
            "uninstalling": (f"Removing PocketOS… {pct}%",                   self.AMBER),
            "success":      ("PocketOS installed! Eject the card and boot.",       self.LIME_DP),
            "removed":      ("PocketOS removed. SD card restored.",                self.LIME_DP),
            "error":        ("Install failed — see log, then retry.",         self.RED),
        }
        txt, col = foot_map.get(phase, ("", "#aebbd3"))
        self._foot_status.config(text=txt, fg=col)
        dot_colors = {self.LIME_DP: self.LIME_DP, self.AMBER: self.AMBER, self.RED: self.RED}
        self._foot_dot.config(fg=dot_colors.get(col, "#7e8db0"))

        # ── Footer A hint ──
        a_labels = {"success": "Finish", "removed": "Finish",
                    "error": "Retry", "idle": "Install", "ready": "Install"}
        self._a_hint.config(text=a_labels.get(phase, "—"))

        # ── Install button label + state ──
        if done:
            self._install_btn.config(text="✓  Eject & Finish", state="normal",
                                      bg=self.LIME_DP, fg="white",
                                      highlightbackground="#3a8c26")
        elif phase == "error":
            self._install_btn.config(text="↩  Retry Install", state="normal",
                                      bg=self.LAV, fg=self.INK,
                                      highlightbackground=self.LAV_BRD)
        elif busy:
            self._install_btn.config(text="⏳  Working…", state="disabled",
                                      bg=self.LAV, fg=self.INK,
                                      highlightbackground=self.LAV_BRD)
        else:
            self._install_btn.config(text="▶  Install PocketOS",
                                      state="normal" if has_sd else "disabled",
                                      bg=self.LAV, fg=self.INK,
                                      highlightbackground=self.LAV_BRD)

        self._remove_btn.config(state="disabled" if busy else
                                 ("normal" if has_sd else "disabled"))

        # ── Progress section ──
        if busy or phase in ("success", "removed", "error"):
            self._progress_wrap.pack(fill="x", before=self._console_label_ref
                                      if hasattr(self, "_console_label_ref") else None)
            phase_labels = {"installing": "Installing", "success": "Installed",
                            "uninstalling": "Removing",  "removed": "Removed",
                            "error": "Failed"}
            self._prog_lbl.config(text=phase_labels.get(phase, "Installing"))
            self._pct_lbl.config(text=f"{pct}%",
                                   fg=self.LIME_DP if pct >= 100 else self.LAV_DEEP)
            self._redraw_segbar()
        else:
            self._progress_wrap.pack_forget()

    # ─────────────────────────────────────────────────────────────────────────
    # Segmented progress bar
    # ─────────────────────────────────────────────────────────────────────────

    def _redraw_segbar(self):
        self._segbar.update_idletasks()
        w = self._segbar.winfo_width()
        h = self._segbar.winfo_height()
        if w < 10:
            return
        self._segbar.delete("all")
        n    = self.SEG_COUNT
        gap  = 3
        pad  = 2
        seg_w = max(2, (w - 2 * pad - (n - 1) * gap) // n)
        segs_on = round((self._pct / 100) * n)
        done    = self._pct >= 100
        for i in range(n):
            x = pad + i * (seg_w + gap)
            if i < segs_on:
                color = self.LIME_DP if done else self.LAV_DEEP
            else:
                color = self.CREAM_IN
            self._segbar.create_rectangle(x, pad, x + seg_w, h - pad,
                                           fill=color, outline="")

    # ─────────────────────────────────────────────────────────────────────────
    # Log console
    # ─────────────────────────────────────────────────────────────────────────

    def _log_line(self, text: str, kind: str = "info"):
        def _do():
            self._console.config(state="normal")
            self._console.insert("end", "> ", "gt")
            self._console.insert("end", text + "\n", kind)
            self._console.see("end")
            self._console.config(state="disabled")
        self.after(0, _do)

    def _clear_log(self):
        def _do():
            self._console.config(state="normal")
            self._console.delete("1.0", "end")
            self._console.config(state="disabled")
        self.after(0, _do)

    def _log(self, text: str):
        """Callable passed to backend functions."""
        kind = "ok" if text.strip().startswith("✓") else \
               "err" if text.strip().startswith("✗") or "ERROR" in text else \
               "done" if "complete" in text.lower() or "done" in text.lower() else "info"
        self._log_line(text, kind)

    # ─────────────────────────────────────────────────────────────────────────
    # SD card detection / validation
    # ─────────────────────────────────────────────────────────────────────────

    def _auto_detect(self):
        candidates = []
        if sys.platform == "win32":
            import string
            for letter in string.ascii_uppercase:
                p = Path(f"{letter}:\\")
                if p.exists() and detect_sd(p):
                    candidates.append(str(p))
        else:
            for mount in [Path("/media"), Path("/mnt"), Path("/Volumes")]:
                if not mount.exists():
                    continue
                try:
                    for child in mount.iterdir():
                        if detect_sd(child):
                            candidates.append(str(child))
                        for grandchild in (child.iterdir() if child.is_dir() else []):
                            if detect_sd(grandchild):
                                candidates.append(str(grandchild))
                except PermissionError:
                    pass
        if candidates:
            self._sd_path.set(candidates[0])

    def _browse_sd(self):
        d = filedialog.askdirectory(title="Select the root of your Miyoo SD card")
        if d:
            self._sd_path.set(d)

    def _browse_roms(self):
        d = filedialog.askdirectory(title="Select folder containing ROM ZIP files",
                                     initialdir=self._rom_src.get() or str(Path.home()))
        if d:
            self._rom_src.set(d)

    def _validate(self):
        raw = self._sd_path.get().strip()
        if not raw:
            self._detect_lbl.config(text="")
            self._card_chip.pack_forget()
            self._onion_frame.pack_forget()
            self._set_phase("idle")
            return

        p = Path(raw)
        if not p.is_dir():
            self._onion_frame.pack_forget()
            self._card_chip.pack_forget()
            return

        if detect_sd(p):
            self._detect_lbl.config(
                text="✓  Valid Miyoo SD card detected — ready",
                fg=self.LIME_DP)
            # Show card chip
            self._chip_name.config(text=p.name or str(p))
            self._chip_det.config(text=str(p))
            self._card_chip.pack(fill="x", pady=(8, 0))
            if self._phase == "idle":
                self._set_phase("ready")
            # Onion check
            if not detect_onion(p):
                self._onion_lbl.config(
                    text="⚠  Onion OS not detected. PocketOS requires Onion OS — "
                         "install it first, then come back.")
                self._onion_frame.pack(fill="x", padx=0, pady=(6, 0))
            else:
                self._onion_frame.pack_forget()
        else:
            self._detect_lbl.config(
                text="⚠  Doesn’t look like the SD card root — "
                     "select the top-level folder",
                fg="#c98a16")
            self._card_chip.pack_forget()
            self._onion_frame.pack_forget()
            self._set_phase("idle")

    # ─────────────────────────────────────────────────────────────────────────
    # ROM import toggle
    # ─────────────────────────────────────────────────────────────────────────

    def _toggle_import(self):
        if self._import_on.get():
            self._rom_row.pack(fill="x", pady=(6, 2))
            self._clean_chk.pack(anchor="w", pady=(2, 0))
            if not self._rom_src.get():
                dl = Path.home() / "Downloads"
                if dl.is_dir():
                    self._rom_src.set(str(dl))
        else:
            self._rom_row.pack_forget()
            self._clean_chk.pack_forget()

    # ─────────────────────────────────────────────────────────────────────────
    # Update check
    # ─────────────────────────────────────────────────────────────────────────

    def _check_for_update(self):
        def _run():
            tag, url = fetch_latest_release()
            if not tag or not url:
                return
            try:
                if version_tuple(tag) > version_tuple(VERSION):
                    self.after(0, lambda: self._show_update_banner(tag, url))
            except Exception:
                pass
        threading.Thread(target=_run, daemon=True).start()

    def _show_update_banner(self, tag, url):
        self._latest_tag = tag
        self._latest_url = url
        self._update_lbl.config(
            text=f"★  Newer version available: {tag}  —  "
                 f"click to install instead of the bundled {VERSION}")
        self._update_btn.config(text=f"Download & Install {tag}")
        self._update_frame.pack(fill="x", pady=(0, 0))

    # ─────────────────────────────────────────────────────────────────────────
    # Button handlers
    # ─────────────────────────────────────────────────────────────────────────

    def _on_install_btn(self):
        if self._phase in ("success", "removed"):
            self._reset()
        else:
            self._do_setup()

    def _reset(self):
        self._sd_path.set("")
        self._rom_src.set("")
        self._import_on.set(False)
        self._card_chip.pack_forget()
        self._onion_frame.pack_forget()
        self._detect_lbl.config(text="")
        self._clear_log()
        self._toggle_import()
        self._set_phase("idle")

    def _get_sd(self) -> Path | None:
        sd = Path(self._sd_path.get().strip())
        if not sd.is_dir():
            messagebox.showerror(
                "No SD Card Selected",
                "Please select the root folder of your Miyoo SD card.\n\n"
                "It's the top-level folder that contains Roms/, BIOS/, etc.")
            return None
        return sd

    # ─────────────────────────────────────────────────────────────────────────
    # Main setup flow
    # ─────────────────────────────────────────────────────────────────────────

    def _do_setup(self):
        sd = self._get_sd()
        if sd is None:
            return
        if not PAYLOAD_BIN.exists():
            messagebox.showerror(
                "Installer Error",
                f"PocketOS payload not found inside the installer.\n"
                f"Try re-downloading from the releases page.\n"
                f"Expected: {PAYLOAD_BIN}")
            return
        self._clear_log()
        self._set_phase("installing", 0)

        do_import = self._import_on.get()
        rom_src   = Path(self._rom_src.get().strip()) if do_import else None
        do_clean  = self._clean_on.get()

        def _run():
            try:
                self._run_setup(sd, rom_src, do_clean)
                self.after(0, lambda: self._set_phase("success", 100))
            except Exception as e:
                self._log_line(f"✗ ERROR: {e}", "err")
                self.after(0, lambda: self._set_phase("error", self._pct))

        threading.Thread(target=_run, daemon=True).start()

    def _run_setup(self, sd: Path, rom_src: Path | None, do_clean: bool):
        log = self._log
        roms_root = sd / "Roms"

        # Phase 1 — install
        log("── Phase 1: Installing PocketOS ──")
        self.after(0, lambda: self._set_phase("installing", 10))
        install(sd, log)
        log("✓ PocketOS installed\n")
        self.after(0, lambda: self._set_phase("installing", 35))

        # Phase 2 — ROM import
        affected_systems: set = set()
        if rom_src and rom_src.is_dir():
            log("── Phase 2: Importing ROMs ──")
            zips = sorted(rom_src.glob("*.zip"))
            log(f"  Found {len(zips)} ZIP(s) in {rom_src}")
            extracted_total = skipped = 0
            for i, zip_path in enumerate(zips):
                ext, candidates = detect_system(zip_path)
                if not candidates:
                    log(f"  [?] {zip_path.name} — unrecognised, skipping")
                    skipped += 1
                    continue
                dest_folder = find_system_folder(roms_root, candidates)
                if dest_folder is None:
                    dest_folder = roms_root / candidates[0]
                    dest_folder.mkdir(parents=True, exist_ok=True)
                    log(f"  [+] Created folder: {dest_folder.name}")
                log(f"  [{dest_folder.name}] {zip_path.name}")
                new_files = extract_zip(zip_path, dest_folder, log)
                extracted_total += len(new_files)
                if new_files:
                    affected_systems.add(dest_folder.name)
                pct = 35 + int(35 * (i + 1) / max(len(zips), 1))
                self.after(0, lambda p=pct: self._set_phase("installing", p))

            log(f"\n  Extracted {extracted_total} file(s), {skipped} unrecognised ZIP(s) skipped")

            if do_clean and affected_systems:
                log("── Phase 2b: Removing duplicate/bad dumps ──")
                total_removed = 0
                for sys_folder in sorted(affected_systems):
                    removed = clean_variants(roms_root / sys_folder, log)
                    if removed:
                        log(f"  {sys_folder}: removed {removed} variant(s)")
                        total_removed += removed
                log(f"  Total removed: {total_removed}")
        else:
            log("── Phase 2: ROM import skipped ──")
            if roms_root.is_dir():
                for d in roms_root.iterdir():
                    if d.is_dir():
                        has_roms = any(f.suffix.lower() in ROM_EXTS
                                       for f in d.iterdir() if f.is_file())
                        if has_roms and not (d / "miyoogamelist.xml").exists():
                            affected_systems.add(d.name)

        self.after(0, lambda: self._set_phase("installing", 70))
        log("")

        # Phase 3 — genre scan
        db_path   = _find_db()
        overrides = _load_overrides()
        if not affected_systems:
            log("── Phase 3: Genre scan skipped (no new ROMs) ──")
        elif not db_path:
            log("── Phase 3: Genre scan skipped (openvgdb.sqlite not found) ──")
        else:
            log("── Phase 3: Scanning genres ──")
            total_added = 0
            for i, sys_folder in enumerate(sorted(affected_systems)):
                added = scan_genres_for_system(roms_root, sys_folder, db_path, log)
                if added:
                    log(f"  {sys_folder}: added {added} entry/entries")
                    total_added += added
                pct = 70 + int(25 * (i + 1) / max(len(affected_systems), 1))
                self.after(0, lambda p=pct: self._set_phase("installing", p))
            log(f"  Total genre entries added: {total_added}")

            if overrides:
                log("── Phase 3b: Applying manual genre overrides ──")
                total_fixed = 0
                for sys_folder in sorted(affected_systems):
                    fixed = apply_overrides(roms_root, sys_folder, overrides, log)
                    if fixed:
                        log(f"  {sys_folder}: fixed {fixed} override(s)")
                        total_fixed += fixed
                log(f"  Total overrides applied: {total_fixed}")

        self.after(0, lambda: self._set_phase("installing", 99))
        log("\n✓ Setup complete.")
        log("  Eject your SD card safely, insert it into your Miyoo Mini Plus, and power on.")
        log("  PocketOS launches automatically.")

    # ─────────────────────────────────────────────────────────────────────────
    # Update install (download latest from GitHub)
    # ─────────────────────────────────────────────────────────────────────────

    def _do_update_install(self):
        sd = self._get_sd()
        if sd is None or not self._latest_url:
            return
        self._clear_log()
        self._set_phase("installing", 0)
        tag = self._latest_tag
        url = self._latest_url

        def _run():
            tmp_dir = None
            try:
                self._log_line(f"► Downloading PocketOS {tag} from GitHub...")
                tmp_dir  = tempfile.mkdtemp(prefix="pocketos_")
                zip_path = Path(tmp_dir) / f"pocketOS-{tag}.zip"

                def _progress(count, block, total):
                    if total > 0:
                        pct = min(90, count * block * 100 // total)
                        self.after(0, lambda p=pct: self._set_phase("installing", p))

                urllib.request.urlretrieve(url, zip_path, reporthook=_progress)
                self._log_line("► Extracting...")
                extract_dir = Path(tmp_dir) / "extracted"
                with zipfile.ZipFile(zip_path) as zf:
                    zf.extractall(extract_dir)
                # Handle ZIPs that wrap everything in a single top-level folder
                src_dir = extract_dir
                if not (src_dir / ".tmp_update").is_dir():
                    for child in extract_dir.iterdir():
                        if child.is_dir() and (child / ".tmp_update").is_dir():
                            src_dir = child
                            break
                self._log_line(f"► Installing PocketOS {tag}...")
                install_from_dir(src_dir, sd, self._log_line)
                self._log_line(f"\n✓ PocketOS {tag} installed!")
                self.after(0, lambda: self._set_phase("success", 100))
                self.after(0, lambda: self._update_frame.pack_forget())
            except Exception as e:
                self._log_line(f"\n✗ ERROR: {e}", "err")
                self.after(0, lambda: self._set_phase("error", self._pct))
            finally:
                if tmp_dir:
                    shutil.rmtree(tmp_dir, ignore_errors=True)

        threading.Thread(target=_run, daemon=True).start()

    # ─────────────────────────────────────────────────────────────────────────
    # Uninstall
    # ─────────────────────────────────────────────────────────────────────────

    def _do_uninstall(self):
        sd = self._get_sd()
        if sd is None:
            return
        if not messagebox.askyesno(
            "Remove PocketOS?",
            "This will remove PocketOS from your SD card.\n\n"
            "The default Onion OS menu will return on next boot.\n"
            "Your games, saves, and settings are not affected.\n\n"
            "Continue?"):
            return
        self._clear_log()
        self._set_phase("uninstalling", 0)

        def _run():
            try:
                self.after(0, lambda: self._set_phase("uninstalling", 30))
                uninstall(sd, self._log_line)
                self.after(0, lambda: self._set_phase("uninstalling", 80))
                self._log_line("\n✓ PocketOS removed. Eject your SD card safely.")
                self.after(0, lambda: self._set_phase("removed", 100))
            except Exception as e:
                self._log_line(f"\n✗ ERROR: {e}", "err")
                self.after(0, lambda: self._set_phase("error", self._pct))

        threading.Thread(target=_run, daemon=True).start()


if __name__ == "__main__":
    app = App()
    app.mainloop()
