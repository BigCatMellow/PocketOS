#!/usr/bin/env python3
"""
PocketOS ROM Importer
Points at a folder of ZIP files, detects each ROM's system by the
extension inside the ZIP, extracts it to the correct Roms/<SYSTEM>/
folder on the SD card, then runs genre scanning and the manual
override pass so Browse by Genre is ready to go.
"""

import os
import re
import sys
import zlib
import zipfile
import sqlite3
import threading
import xml.etree.ElementTree as ET
from xml.dom import minidom
from pathlib import Path
import tkinter as tk
from tkinter import ttk, filedialog, scrolledtext, messagebox

try:
    from .onion_systems import (
        ROM_EXTENSIONS, candidates_for_extension, extensions_for_folder, openvgdb_system_name,
    )
    from .genre_overrides import load_overrides as load_genre_overrides
    from .rom_safety import (
        GamelistError, crc32_of, extract_zip_roms, index_games,
        load_gamelist_tree, write_xml_atomic,
    )
except ImportError:  # Direct script and PyInstaller execution.
    from onion_systems import (
        ROM_EXTENSIONS, candidates_for_extension, extensions_for_folder, openvgdb_system_name,
    )
    from genre_overrides import load_overrides as load_genre_overrides
    from rom_safety import (
        GamelistError, crc32_of, extract_zip_roms, index_games,
        load_gamelist_tree, write_xml_atomic,
    )

# ── Onion ROM/system contract ─────────────────────────────────────────────────
ROM_EXTS = set(ROM_EXTENSIONS)
DOC_NAMES = {"readme", "license", "changelog", "credits", "notes", "info", "manual"}

# ── Genre scanning helpers (inline so we don't depend on genre_scanner.py) ───

QUERY_CRC = """
    SELECT r.releaseTitleName, r.releaseGenre
    FROM RELEASES r
    JOIN ROMs ro ON r.romID = ro.romID
    JOIN SYSTEMS s ON ro.systemID = s.systemID
    WHERE UPPER(ro.romHashCRC) = ?
      AND s.systemName = ?
    LIMIT 1
"""
QUERY_FILENAME = """
    SELECT r.releaseTitleName, r.releaseGenre
    FROM RELEASES r
    JOIN ROMs ro ON r.romID = ro.romID
    JOIN SYSTEMS s ON ro.systemID = s.systemID
    WHERE ro.romExtensionlessFileName = ?
      AND s.systemName = ?
    LIMIT 1
"""

def db_lookup(db, rom: Path, system_name: str):
    crc = crc32_of(rom)
    if crc:
        row = db.execute(QUERY_CRC, (crc, system_name)).fetchone()
        if row and row[0]:
            return row[0], row[1] or "Unsorted"
    row = db.execute(QUERY_FILENAME, (rom.stem, system_name)).fetchone()
    if row and row[0]:
        return row[0], row[1] or "Unsorted"
    return None

# ── Core importer logic ───────────────────────────────────────────────────────

def find_system_folder(roms_root: Path, candidates: list) -> Path | None:
    """Return the first candidate folder that exists under roms_root."""
    for name in candidates:
        p = roms_root / name
        if p.is_dir():
            return p
    return None

def _is_doc_file(name: str) -> bool:
    """True if this file looks like documentation rather than a ROM."""
    stem = Path(name).stem.lower()
    return stem in DOC_NAMES or any(stem.startswith(d) for d in DOC_NAMES)

def detect_system(zip_path: Path) -> tuple[str | None, list[str]]:
    """Return only extension classifications that are safe without user input."""
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

def extract_zip(zip_path: Path, dest_folder: Path, log) -> list[Path]:
    allowed = extensions_for_folder(dest_folder.name)
    if not allowed:
        log(f"  ERROR: no safe extraction extension contract for {dest_folder.name}")
        return []
    return extract_zip_roms(zip_path, dest_folder, allowed, log)

def scan_genres_for_system(roms_root: Path, system_folder: str,
                           db_path: Path, log) -> int:
    """Add missing genre entries without discarding existing gamelist metadata."""
    system_dir = roms_root / system_folder
    gamelist = system_dir / "miyoogamelist.xml"
    system_name = openvgdb_system_name(system_folder)
    if not system_name:
        log(f"  genre scan: no DB mapping for {system_folder}, skipping")
        return 0
    try:
        db = sqlite3.connect(str(db_path))
    except Exception as exc:
        log(f"  genre scan: DB open failed: {exc}")
        return 0
    try:
        tree = load_gamelist_tree(gamelist)
    except GamelistError as exc:
        db.close()
        log(f"  genre scan: {exc}")
        return 0
    root = tree.getroot()
    existing = index_games(root)
    added = 0
    for rom in sorted(system_dir.iterdir()):
        if rom.suffix.lower() in {".xml", ".db", ".txt", ""} or not rom.is_file():
            continue
        if rom.name in existing:
            continue
        rom_system_name = openvgdb_system_name(system_folder, rom.suffix) or system_name
        result = db_lookup(db, rom, rom_system_name)
        name, genre = result if result else (rom.stem, "Unsorted")
        el = ET.SubElement(root, "game")
        ET.SubElement(el, "path").text = "./" + rom.name
        ET.SubElement(el, "name").text = name
        ET.SubElement(el, "genre").text = genre
        added += 1
    db.close()
    if added:
        write_xml_atomic(tree, gamelist)
        log(f"  genre scan: {system_folder} — added {added} entry/entries")
    return added

def apply_overrides_for_system(roms_root: Path, system_folder: str,
                               overrides: dict, log) -> int:
    """Apply manual genre overrides. Returns number of fixes."""
    gamelist = roms_root / system_folder / "miyoogamelist.xml"
    if not gamelist.exists():
        return 0
    try:
        tree = ET.parse(gamelist)
    except Exception as e:
        log(f"  override: parse error {e}")
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
        log(f"  overrides: {system_folder} — fixed {changed} game(s)")
    return changed


# ── Variant cleanup ───────────────────────────────────────────────────────────

def _base_name(stem: str) -> str:
    """Group key: everything before the first region/flag tag."""
    m = re.search(r' [\(\[]', stem)
    return stem[:m.start()].strip().lower() if m else stem.strip().lower()

def _rom_score(name: str) -> int:
    """Higher = better. Verified good dumps win; bad/hacked/translated dumps lose."""
    n = name.upper()
    score = 0
    _end = r'(?:[ \[\(\.]|$)'  # tag followed by space, bracket, dot (extension), or end
    if '[!]'      in n:                              score += 100
    if re.search(r'\(USA\)|\(U\)' + _end,   n):     score +=  20
    if '(WORLD)'  in n:                              score +=  15
    if re.search(r'\(EUROPE\)|\(E\)' + _end, n):    score +=  10
    if re.search(r'\(JAPAN\)|\(J\)' + _end,  n):    score +=   5
    if re.search(r'\[B',   n):                   score -= 1000  # bad dump
    if re.search(r'\[O',   n):                   score -=  500  # overdump
    if re.search(r'\[H',   n):                   score -=  200  # hack
    if re.search(r'\[T\d', n):                   score -=  150  # trainer
    if re.search(r'\[T[+\-]', n):                score -=   80  # translation
    if re.search(r'\[A\d', n):                   score -=   50  # alternate
    if re.search(r'\[F\d', n):                   score -=   30  # fix
    if re.search(r'\[P\d', n):                   score -=  100  # pirate tag
    if '(PD)'     in n:                          score -=   20  # public domain
    if '(PIRATE)' in n:                          score -=  100
    # "Hack" or "Trainer" in the title itself (not a flag tag)
    if re.search(r'\bHACK\b',    n):             score -=  150
    if re.search(r'\bTRAINER\b', n):             score -=  100
    return score

def find_variants_to_remove(folder: Path) -> tuple[list[Path], list[Path]]:
    """
    Groups ROMs in folder by base title, keeps the highest-scored file per
    group, returns (to_remove, to_keep).
    Singles (no duplicates in the group) are always kept untouched.
    """
    rom_files = [f for f in sorted(folder.iterdir())
                 if f.is_file() and f.suffix.lower() in ROM_EXTS]

    groups: dict[str, list[Path]] = {}
    for f in rom_files:
        key = _base_name(f.stem)
        groups.setdefault(key, []).append(f)

    to_remove, to_keep = [], []
    for files in groups.values():
        if len(files) == 1:
            to_keep.extend(files)
            continue
        scored = sorted(files, key=lambda f: (-_rom_score(f.name), f.name))
        to_keep.append(scored[0])
        to_remove.extend(scored[1:])

    return to_remove, to_keep

def clean_variants(folder: Path, log) -> int:
    """Report possible variants without modifying user ROM files."""
    to_remove, to_keep = find_variants_to_remove(folder)
    if not to_remove:
        return 0
    if to_keep:
        log(f"  suggested keep → {to_keep[0].name}")
    for path in to_remove:
        log(f"  possible variant (no action) → {path.name}")
    return len(to_remove)


# ── GUI ───────────────────────────────────────────────────────────────────────

class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("PocketOS ROM Importer")
        self.resizable(False, False)
        self._build_ui()
        self._detect_defaults()

    def _build_ui(self):
        pad = {"padx": 12, "pady": 6}

        # Source folder
        tk.Label(self, text="ZIP source folder:").grid(row=0, column=0, sticky="w", **pad)
        self.src_var = tk.StringVar()
        tk.Entry(self, textvariable=self.src_var, width=48).grid(row=0, column=1, **pad)
        tk.Button(self, text="Browse…", command=self._pick_src).grid(row=0, column=2, **pad)

        # SD card root
        tk.Label(self, text="SD card root:").grid(row=1, column=0, sticky="w", **pad)
        self.sd_var = tk.StringVar()
        tk.Entry(self, textvariable=self.sd_var, width=48).grid(row=1, column=1, **pad)
        tk.Button(self, text="Browse…", command=self._pick_sd).grid(row=1, column=2, **pad)

        # Log
        self.log_box = scrolledtext.ScrolledText(self, width=72, height=22,
                                                 state="disabled", font=("Courier New", 10))
        self.log_box.grid(row=2, column=0, columnspan=3, padx=12, pady=6)

        # Options
        self.clean_var = tk.BooleanVar(value=False)
        tk.Checkbutton(self, text="Analyze possible duplicate/bad/hack variants (no deletion)",
                       variable=self.clean_var).grid(row=3, column=0, columnspan=3, pady=(4, 0))

        # Progress bar
        self.progress = ttk.Progressbar(self, mode="indeterminate")
        self.progress.grid(row=4, column=0, columnspan=3, sticky="ew", padx=12, pady=(6, 0))

        # Run button
        self.run_btn = tk.Button(self, text="Import ROMs", width=20,
                                 command=self._run, bg="#4FA85E", fg="white",
                                 font=(None, 11, "bold"))
        self.run_btn.grid(row=5, column=0, columnspan=3, pady=10)

    def _detect_defaults(self):
        downloads = Path.home() / "Downloads"
        if downloads.is_dir():
            self.src_var.set(str(downloads))
        # Scan common mount roots for a Miyoo SD card
        import sys as _sys
        mount_roots = []
        if _sys.platform == "win32":
            import string
            mount_roots = [Path(f"{c}:\\") for c in string.ascii_uppercase]
        else:
            for base in [Path("/media"), Path("/mnt"), Path("/Volumes")]:
                if not base.exists():
                    continue
                try:
                    for child in base.iterdir():
                        mount_roots.append(child)
                        if child.is_dir():
                            mount_roots.extend(child.iterdir())
                except PermissionError:
                    pass
        for candidate in mount_roots:
            try:
                if candidate.is_dir() and (candidate / "Roms").is_dir() and (candidate / ".tmp_update").is_dir():
                    self.sd_var.set(str(candidate))
                    break
            except PermissionError:
                pass

    def _pick_src(self):
        d = filedialog.askdirectory(title="Select ZIP source folder",
                                    initialdir=self.src_var.get() or Path.home())
        if d:
            self.src_var.set(d)

    def _pick_sd(self):
        d = filedialog.askdirectory(title="Select SD card root",
                                    initialdir=self.sd_var.get() or "/media")
        if d:
            self.sd_var.set(d)

    def log(self, msg: str):
        def _do():
            self.log_box.config(state="normal")
            self.log_box.insert("end", msg + "\n")
            self.log_box.see("end")
            self.log_box.config(state="disabled")
        self.after(0, _do)

    def _run(self):
        self.run_btn.config(state="disabled")
        self.progress.start()
        threading.Thread(target=self._import_thread, daemon=True).start()

    def _import_thread(self):
        try:
            self._do_import()
        except Exception as e:
            self.log(f"\nFATAL ERROR: {e}")
        finally:
            self.after(0, lambda: self.run_btn.config(state="normal"))
            self.after(0, self.progress.stop)

    def _do_import(self):
        src  = Path(self.src_var.get().strip())
        sd   = Path(self.sd_var.get().strip())

        # Validate
        if not src.is_dir():
            self.log("ERROR: source folder does not exist.")
            return
        if not sd.is_dir() or not (sd / "Roms").is_dir():
            self.log("ERROR: SD card root not found or missing Roms/ folder.")
            return

        roms_root = sd / "Roms"

        # Find OpenVGDB
        db_path = Path(__file__).parent / "openvgdb.sqlite"
        if not db_path.exists():
            self.log("WARNING: openvgdb.sqlite not found — genre scanning will be skipped.")
            self.log(f"  Expected at: {db_path}")
            db_path = None

        # Data-only manual genre overrides.
        overrides = load_genre_overrides()

        # Scan source for ZIPs
        zips = sorted(src.glob("*.zip"))
        if not zips:
            self.log(f"No .zip files found in {src}")
            return

        self.log(f"Found {len(zips)} ZIP(s) in {src}")
        self.log(f"SD card: {sd}\n")

        affected_systems = set()
        skipped = extracted_total = 0

        for zip_path in zips:
            ext, candidates = detect_system(zip_path)
            if not candidates:
                self.log(f"[?] {zip_path.name} — unrecognised, skipping")
                skipped += 1
                continue

            dest_folder = find_system_folder(roms_root, candidates)
            if dest_folder is None:
                self.log(f"[{candidates[0]}] {zip_path.name} — Onion system folder is not installed; skipping")
                skipped += 1
                continue

            self.log(f"[{dest_folder.name}] {zip_path.name}")
            new_files = extract_zip(zip_path, dest_folder, self.log)
            extracted_total += len(new_files)
            if new_files:
                affected_systems.add(dest_folder.name)

        self.log(f"\n── Extraction done: {extracted_total} file(s) extracted, {skipped} ZIP(s) skipped ──\n")

        if not affected_systems:
            self.log("No new files extracted — nothing to scan.")
            return

        # Genre scan
        if db_path:
            self.log("── Genre scanning new ROMs ──")
            for sys_folder in sorted(affected_systems):
                scan_genres_for_system(roms_root, sys_folder, db_path, self.log)
        else:
            self.log("── Skipping genre scan (no openvgdb.sqlite) ──")

        # Override pass
        if overrides:
            self.log("\n── Applying manual genre overrides ──")
            for sys_folder in sorted(affected_systems):
                apply_overrides_for_system(roms_root, sys_folder, overrides, self.log)

        # Variant cleanup (optional)
        if self.clean_var.get() and affected_systems:
            self.log("\n── Removing duplicate/bad/hack variants ──")
            total_removed = 0
            for sys_folder in sorted(affected_systems):
                folder = roms_root / sys_folder
                removed = clean_variants(folder, self.log)
                if removed:
                    self.log(f"  {sys_folder}: removed {removed} variant(s)")
                    total_removed += removed
            self.log(f"  total removed: {total_removed}")

        self.log("\n✓ All done. Eject SD card and boot.")


if __name__ == "__main__":
    app = App()
    app.mainloop()
