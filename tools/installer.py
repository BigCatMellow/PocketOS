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
from tkinter import ttk, filedialog, scrolledtext, messagebox

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


# ── GUI ───────────────────────────────────────────────────────────────────────

class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title(f"PocketOS Setup {VERSION}")
        self.resizable(False, False)
        self.configure(bg="#1e1e2e")
        self._sd_path    = tk.StringVar()
        self._rom_src    = tk.StringVar()
        self._import_on  = tk.BooleanVar(value=False)
        self._clean_on   = tk.BooleanVar(value=True)
        self._latest_tag = None
        self._latest_url = None
        self._build_ui()
        self._center()
        self._auto_detect()
        self._check_for_update()

    def _center(self):
        self.update_idletasks()
        w, h = self.winfo_width(), self.winfo_height()
        self.geometry(f"+{(self.winfo_screenwidth()-w)//2}+{(self.winfo_screenheight()-h)//2}")

    def _build_ui(self):
        PAD = 16
        FG  = "#cdd6f4"
        BG  = "#1e1e2e"
        ENT = "#313244"
        ACC = "#89b4fa"
        BTN = "#45475a"
        RED = "#f38ba8"
        SUB = "#a6adc8"
        DIM = "#6c7086"
        GRN = "#a6e3a1"

        # Logo
        LOGO = (
            r" ____             _        _    ___  ____  " + "\n"
            r"|  _ \ ___   ___ | | _____| |_ / _ \/ ___| " + "\n"
            r"| |_) / _ \ / __|| |/ / _ \ __| | | \___ \ " + "\n"
            r"|  __/ (_) | (__ |   <  __/ |_| |_| |___) |" + "\n"
            r"|_|   \___/ \___||_|\_\___|\__|\___/|____/ " + "\n"
            f"                         Setup  {VERSION}  "
        )
        tk.Label(self, text=LOGO, font=("Courier", 9, "bold"),
                 fg=ACC, bg=BG, justify="center").pack(pady=(PAD, 4))
        tk.Label(self,
                 text="A minimal launcher for the Miyoo Mini Plus  ·  Built on Onion OS",
                 font=("Helvetica", 9), fg=DIM, bg=BG, justify="center").pack(pady=(0, 8))

        # Update banner
        self._update_frame = tk.Frame(self, bg="#1e3a5f", padx=PAD, pady=8)
        self._update_lbl   = tk.Label(self._update_frame, text="", fg="#89dceb",
                                       bg="#1e3a5f", font=("Helvetica", 9), anchor="w", justify="left")
        self._update_lbl.pack(side="left", fill="x", expand=True)
        self._update_btn = tk.Button(self._update_frame, text="",
                                      command=self._do_update_install,
                                      bg=BTN, fg=GRN, relief="flat",
                                      padx=8, cursor="hand2", font=("Helvetica", 9, "bold"))
        self._update_btn.pack(side="right")
        self._update_frame.pack_forget()

        ttk.Separator(self, orient="horizontal").pack(fill="x", padx=PAD)

        # ── Step 1: SD card ───────────────────────────────────────────────────
        f1 = tk.Frame(self, bg=BG, padx=PAD, pady=10)
        f1.pack(fill="x")
        tk.Label(f1, text="Step 1 — Select your SD card",
                 fg=FG, bg=BG, font=("Helvetica", 10, "bold"), anchor="w").pack(fill="x")
        tk.Label(f1, text="The root of the card — contains Roms/ and BIOS/.",
                 fg=DIM, bg=BG, font=("Helvetica", 9), anchor="w").pack(fill="x", pady=(2, 4))
        row = tk.Frame(f1, bg=BG)
        row.pack(fill="x")
        self._sd_entry = tk.Entry(row, textvariable=self._sd_path, width=52,
                                   bg=ENT, fg=FG, insertbackground=FG, relief="flat",
                                   font=("Helvetica", 10))
        self._sd_entry.pack(side="left", fill="x", expand=True, ipady=4)
        tk.Button(row, text="Browse…", command=self._browse_sd,
                  bg=BTN, fg=FG, relief="flat", padx=10, cursor="hand2").pack(side="left", padx=(6, 0))
        self._detect_lbl = tk.Label(f1, text="", fg=SUB, bg=BG, font=("Helvetica", 9))
        self._detect_lbl.pack(anchor="w", pady=(4, 0))

        # Onion warning
        self._onion_frame = tk.Frame(self, bg="#313244", padx=PAD, pady=8)
        self._onion_lbl   = tk.Label(self._onion_frame, text="", fg="#fab387", bg="#313244",
                                      font=("Helvetica", 9), justify="left", anchor="w", wraplength=400)
        self._onion_lbl.pack(side="left", fill="x", expand=True)
        tk.Button(self._onion_frame, text="Get Onion OS →",
                  command=lambda: webbrowser.open(ONION_URL),
                  bg=BTN, fg=ACC, relief="flat", padx=8, cursor="hand2",
                  font=("Helvetica", 9)).pack(side="right")
        self._onion_frame.pack_forget()

        ttk.Separator(self, orient="horizontal").pack(fill="x", padx=PAD)

        # ── Step 2: ROM import (optional) ─────────────────────────────────────
        f2 = tk.Frame(self, bg=BG, padx=PAD, pady=10)
        f2.pack(fill="x")
        tk.Label(f2, text="Step 2 — Import ROMs  (optional)",
                 fg=FG, bg=BG, font=("Helvetica", 10, "bold"), anchor="w").pack(fill="x")
        tk.Label(f2,
                 text="Point to a folder of ZIP files. PocketOS will detect each system automatically,\n"
                      "extract the ROMs, scan genres, and set up Browse by Genre.",
                 fg=DIM, bg=BG, font=("Helvetica", 9), anchor="w", justify="left").pack(fill="x", pady=(2, 6))

        chk_row = tk.Frame(f2, bg=BG)
        chk_row.pack(fill="x")
        tk.Checkbutton(chk_row, text="Import ROMs from folder",
                       variable=self._import_on, command=self._toggle_import,
                       fg=FG, bg=BG, selectcolor=ENT, activebackground=BG,
                       font=("Helvetica", 9)).pack(side="left")

        self._rom_row = tk.Frame(f2, bg=BG)
        self._rom_entry = tk.Entry(self._rom_row, textvariable=self._rom_src, width=44,
                                    bg=ENT, fg=FG, insertbackground=FG, relief="flat",
                                    font=("Helvetica", 10))
        self._rom_entry.pack(side="left", fill="x", expand=True, ipady=4)
        tk.Button(self._rom_row, text="Browse…", command=self._browse_roms,
                  bg=BTN, fg=FG, relief="flat", padx=10, cursor="hand2").pack(side="left", padx=(6, 0))

        self._clean_chk = tk.Checkbutton(f2,
                          text="Remove duplicate/bad/hacked dumps — keep the best version of each game",
                          variable=self._clean_on,
                          fg=SUB, bg=BG, selectcolor=ENT, activebackground=BG,
                          font=("Helvetica", 9))

        ttk.Separator(self, orient="horizontal").pack(fill="x", padx=PAD)

        # ── Step 3: Action buttons ────────────────────────────────────────────
        f3 = tk.Frame(self, bg=BG, padx=PAD, pady=12)
        f3.pack(fill="x")
        tk.Label(f3, text="Step 3 — Run setup",
                 fg=FG, bg=BG, font=("Helvetica", 10, "bold"), anchor="w").pack(fill="x", pady=(0, 8))

        btn_row = tk.Frame(f3, bg=BG)
        btn_row.pack(fill="x")
        self._setup_btn = tk.Button(btn_row, text="▶  Set Up PocketOS",
                                     command=self._do_setup,
                                     bg=ACC, fg="#1e1e2e",
                                     font=("Helvetica", 12, "bold"),
                                     relief="flat", padx=16, pady=10, cursor="hand2")
        self._setup_btn.pack(side="left", fill="x", expand=True, padx=(0, 6))
        self._remove_btn = tk.Button(btn_row, text="Remove",
                                      command=self._do_uninstall,
                                      bg=BTN, fg=RED,
                                      font=("Helvetica", 11),
                                      relief="flat", padx=16, pady=10, cursor="hand2")
        self._remove_btn.pack(side="left")

        tk.Label(f3,
                 text="Set Up PocketOS: installs the launcher, imports ROMs (if selected), scans genres.\n"
                      "Remove: uninstalls PocketOS and returns the default Onion menu.",
                 fg=DIM, bg=BG, font=("Helvetica", 8), anchor="w", justify="left").pack(fill="x", pady=(4, 0))

        # Progress + log
        self._progress = ttk.Progressbar(self, mode="indeterminate")
        self._progress.pack(fill="x", padx=PAD, pady=(4, 2))
        tk.Label(self, text="Progress log", fg=DIM, bg=BG,
                 font=("Helvetica", 8), anchor="w", padx=PAD).pack(fill="x")
        self._log = scrolledtext.ScrolledText(self, height=12, width=68,
                                               bg="#181825", fg=FG,
                                               font=("Courier", 9), relief="flat",
                                               state="disabled")
        self._log.pack(padx=PAD, pady=(2, PAD), fill="both")

        self._status = tk.Label(self,
                                 text="Insert your SD card and select it above to get started.",
                                 fg=SUB, bg="#181825", font=("Helvetica", 9),
                                 anchor="w", padx=8)
        self._status.pack(fill="x", side="bottom")

        self._sd_path.trace_add("write", lambda *_: self._validate())
        self._toggle_import()

    # ── Toggle ROM import section ─────────────────────────────────────────────

    def _toggle_import(self):
        if self._import_on.get():
            self._rom_row.pack(fill="x", pady=(4, 2))
            self._clean_chk.pack(fill="x", pady=(2, 0))
            if not self._rom_src.get():
                dl = Path.home() / "Downloads"
                if dl.is_dir():
                    self._rom_src.set(str(dl))
        else:
            self._rom_row.pack_forget()
            self._clean_chk.pack_forget()

    # ── Version check ─────────────────────────────────────────────────────────

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
            text=f"★  A newer version is available: {tag}\n"
                 f"   Click to download and install {tag} instead of the bundled {VERSION}.")
        self._update_btn.config(text=f"Download & Install {tag}")
        self._update_frame.pack(fill="x", padx=16, pady=(0, 6))

    # ── SD card auto-detect & validation ──────────────────────────────────────

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
                for child in mount.iterdir():
                    if detect_sd(child):
                        candidates.append(str(child))
                    for grandchild in (child.iterdir() if child.is_dir() else []):
                        if detect_sd(grandchild):
                            candidates.append(str(grandchild))
        if candidates:
            self._sd_path.set(candidates[0])
            self._detect_lbl.config(
                text="✓ Miyoo SD card detected automatically — ready to install", fg="#a6e3a1")
            self._status.config(text="SD card found. Click Set Up PocketOS when ready.")

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
        p = Path(self._sd_path.get().strip())
        if not p.is_dir():
            self._onion_frame.pack_forget()
            return
        if detect_sd(p):
            self._detect_lbl.config(text="✓ Looks like a valid Miyoo SD card — ready", fg="#a6e3a1")
            self._status.config(text="SD card selected. Click Set Up PocketOS when ready.")
        else:
            self._detect_lbl.config(
                text="⚠  Doesn't look like the SD card root — select the top-level folder",
                fg="#fab387")
        if detect_sd(p) and not detect_onion(p):
            self._onion_lbl.config(
                text="⚠  Onion OS not detected on this card.\n"
                     "PocketOS runs on top of Onion OS — install Onion first, then come back here.")
            self._onion_frame.pack(fill="x", padx=16, pady=(0, 8))
        else:
            self._onion_frame.pack_forget()

    # ── Log / busy helpers ────────────────────────────────────────────────────

    def _log_line(self, text: str):
        def _do():
            self._log.config(state="normal")
            self._log.insert("end", text + "\n")
            self._log.see("end")
            self._log.config(state="disabled")
        self.after(0, _do)

    def _set_busy(self, busy: bool):
        state = "disabled" if busy else "normal"
        self._setup_btn.config(state=state)
        self._remove_btn.config(state=state)
        if busy:
            self._progress.start()
        else:
            self._progress.stop()

    def _clear_log(self):
        self._log.config(state="normal")
        self._log.delete("1.0", "end")
        self._log.config(state="disabled")

    # ── Main setup flow ───────────────────────────────────────────────────────

    def _do_setup(self):
        sd = self._get_sd()
        if sd is None:
            return
        if not PAYLOAD_BIN.exists():
            messagebox.showerror("Installer Error",
                                 f"PocketOS payload not found inside the installer.\n"
                                 f"Try re-downloading from the releases page.\n"
                                 f"Expected: {PAYLOAD_BIN}")
            return
        self._clear_log()
        self._set_busy(True)
        self._status.config(text="Running setup — don't eject the SD card...")

        do_import = self._import_on.get()
        rom_src   = Path(self._rom_src.get().strip()) if do_import else None
        do_clean  = self._clean_on.get()

        def _run():
            try:
                self._run_setup(sd, rom_src, do_clean)
                self.after(0, lambda: self._status.config(
                    text="✓ Done! Eject your SD card safely, then power on your device."))
            except Exception as e:
                self._log_line(f"\n✗ ERROR: {e}")
                self.after(0, lambda: self._status.config(text="Setup failed — check log for details."))
            finally:
                self.after(0, lambda: self._set_busy(False))

        threading.Thread(target=_run, daemon=True).start()

    def _run_setup(self, sd: Path, rom_src: Path | None, do_clean: bool):
        log = self._log_line
        roms_root = sd / "Roms"

        # ── Phase 1: Install PocketOS ─────────────────────────────────────────
        log("── Phase 1: Installing PocketOS ──")
        install(sd, log)
        log("✓ PocketOS installed\n")

        # ── Phase 2: Import ROMs ──────────────────────────────────────────────
        affected_systems = set()

        if rom_src and rom_src.is_dir():
            log("── Phase 2: Importing ROMs ──")
            zips = sorted(rom_src.glob("*.zip"))
            log(f"  Found {len(zips)} ZIP(s) in {rom_src}")
            extracted_total = skipped = 0

            for zip_path in zips:
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

            log(f"\n  Extracted {extracted_total} file(s), {skipped} unrecognised ZIP(s) skipped")

            if do_clean and affected_systems:
                log("\n── Phase 2b: Removing duplicate/bad dumps ──")
                total_removed = 0
                for sys_folder in sorted(affected_systems):
                    removed = clean_variants(roms_root / sys_folder, log)
                    if removed:
                        log(f"  {sys_folder}: removed {removed} variant(s)")
                        total_removed += removed
                log(f"  Total removed: {total_removed}")
        else:
            log("── Phase 2: ROM import skipped ──")
            # Still scan all existing systems for missing genre data
            if roms_root.is_dir():
                for d in roms_root.iterdir():
                    if d.is_dir():
                        has_roms = any(f.suffix.lower() in ROM_EXTS for f in d.iterdir() if f.is_file())
                        if has_roms and not (d / "miyoogamelist.xml").exists():
                            affected_systems.add(d.name)

        log("")

        # ── Phase 3: Genre scan ───────────────────────────────────────────────
        db_path   = _find_db()
        overrides = _load_overrides()

        if not affected_systems:
            log("── Phase 3: Genre scan skipped (no new ROMs) ──")
        elif not db_path:
            log("── Phase 3: Genre scan skipped (openvgdb.sqlite not found) ──")
            log(f"  Expected at: {Path(__file__).parent / 'openvgdb.sqlite'}")
        else:
            log("── Phase 3: Scanning genres ──")
            total_added = 0
            for sys_folder in sorted(affected_systems):
                added = scan_genres_for_system(roms_root, sys_folder, db_path, log)
                if added:
                    log(f"  {sys_folder}: added {added} entry/entries")
                    total_added += added
            log(f"  Total genre entries added: {total_added}")

            if overrides:
                log("\n── Phase 3b: Applying manual genre overrides ──")
                total_fixed = 0
                for sys_folder in sorted(affected_systems):
                    fixed = apply_overrides(roms_root, sys_folder, overrides, log)
                    if fixed:
                        log(f"  {sys_folder}: fixed {fixed} override(s)")
                        total_fixed += fixed
                log(f"  Total overrides applied: {total_fixed}")

        log("\n✓ Setup complete.")
        log("  Eject your SD card safely, insert it into your Miyoo Mini Plus, and power on.")
        log("  PocketOS launches automatically — no extra steps needed on the device.")

    # ── Update install (download latest from GitHub) ──────────────────────────

    def _do_update_install(self):
        sd = self._get_sd()
        if sd is None or not self._latest_url:
            return
        self._clear_log()
        self._set_busy(True)
        tag = self._latest_tag
        url = self._latest_url
        self._status.config(text=f"Downloading {tag} — don't eject the SD card...")

        def _run():
            tmp_dir = None
            try:
                self._log_line(f"► Downloading PocketOS {tag} from GitHub...")
                tmp_dir  = tempfile.mkdtemp(prefix="pocketos_")
                zip_path = Path(tmp_dir) / f"pocketOS-{tag}.zip"

                def _progress(count, block, total):
                    if total > 0:
                        pct = min(100, count * block * 100 // total)
                        self._log_line(f"  {pct}%  ({count*block/1048576:.1f} / {total/1048576:.1f} MB)")

                urllib.request.urlretrieve(url, zip_path, reporthook=_progress)
                self._log_line("► Extracting...")
                extract_dir = Path(tmp_dir) / "extracted"
                with zipfile.ZipFile(zip_path) as zf:
                    zf.extractall(extract_dir)
                self._log_line(f"► Installing PocketOS {tag}...")
                install_from_dir(extract_dir, sd, self._log_line)
                self._log_line(f"\n✓ PocketOS {tag} installed!")
                self.after(0, lambda: self._status.config(
                    text=f"✓ PocketOS {tag} installed! Eject safely, then power on."))
                self.after(0, lambda: self._update_frame.pack_forget())
            except Exception as e:
                self._log_line(f"\n✗ ERROR: {e}")
                self.after(0, lambda: self._status.config(text="Download failed — check log."))
            finally:
                if tmp_dir:
                    shutil.rmtree(tmp_dir, ignore_errors=True)
                self.after(0, lambda: self._set_busy(False))

        threading.Thread(target=_run, daemon=True).start()

    # ── Uninstall ─────────────────────────────────────────────────────────────

    def _do_uninstall(self):
        sd = self._get_sd()
        if sd is None:
            return
        if not messagebox.askyesno("Remove PocketOS?",
                                    "This will remove PocketOS from your SD card.\n\n"
                                    "The default Onion OS menu will return on next boot.\n"
                                    "Your games, saves, and settings are not affected.\n\n"
                                    "Continue?"):
            return
        self._clear_log()
        self._set_busy(True)
        self._status.config(text="Removing PocketOS — don't eject the SD card...")

        def _run():
            try:
                uninstall(sd, self._log_line)
                self._log_line("\n✓ PocketOS removed. Eject your SD card safely.")
                self.after(0, lambda: self._status.config(text="✓ Done! Eject safely, then power on."))
            except Exception as e:
                self._log_line(f"\n✗ ERROR: {e}")
                self.after(0, lambda: self._status.config(text="Removal failed — check log."))
            finally:
                self.after(0, lambda: self._set_busy(False))

        threading.Thread(target=_run, daemon=True).start()

    # ── Shared helpers ────────────────────────────────────────────────────────

    def _get_sd(self) -> Path | None:
        sd = Path(self._sd_path.get().strip())
        if not sd.is_dir():
            messagebox.showerror("No SD Card Selected",
                                 "Please select the root folder of your Miyoo SD card first.\n\n"
                                 "It's the top-level folder that contains Roms/, BIOS/, etc.")
            return None
        return sd


if __name__ == "__main__":
    app = App()
    app.mainloop()
