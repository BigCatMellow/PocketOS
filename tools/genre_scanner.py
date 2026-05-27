#!/usr/bin/env python3
"""
PocketOS Genre Scanner
Scans your Miyoo Mini Plus ROM library against OpenVGDB and writes
miyoogamelist.xml files so PocketOS can browse games by genre.
"""

import os
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
import urllib.request

# ── OpenVGDB download URL (latest release asset) ─────────────────────────────
OPENVGDB_URL = "https://github.com/OpenVGDB/OpenVGDB/releases/download/v29.0/openvgdb.sqlite.zip"

# ── System folder → OpenVGDB system name ─────────────────────────────────────
SYSTEM_MAP = {
    "FC": "Nintendo Entertainment System",
    "NES": "Nintendo Entertainment System",
    "SFC": "Nintendo Super Nintendo Entertainment System",
    "SNES": "Nintendo Super Nintendo Entertainment System",
    "N64": "Nintendo 64",
    "GB": "Nintendo Game Boy",
    "SGB": "Nintendo Game Boy",
    "GBC": "Nintendo Game Boy Color",
    "GBA": "Nintendo Game Boy Advance",
    "NDS": "Nintendo DS",
    "VBOY": "Nintendo Virtual Boy",
    "MD": "Sega Genesis/Mega Drive",
    "GEN": "Sega Genesis/Mega Drive",
    "GENESIS": "Sega Genesis/Mega Drive",
    "SMS": "Sega Master System",
    "GG": "Sega Game Gear",
    "SATURN": "Sega Saturn",
    "SCD": "Sega CD/Mega-CD",
    "32X": "Sega 32X",
    "PS": "Sony PlayStation",
    "PS1": "Sony PlayStation",
    "PSX": "Sony PlayStation",
    "PSP": "Sony PlayStation Portable",
    "PCE": "NEC PC Engine/TurboGrafx-16",
    "PCECD": "NEC PC Engine CD/TurboGrafx-CD",
    "PCFX": "NEC PC-FX",
    "SGFX": "NEC SuperGrafx",
    "NEOGEO": "SNK Neo Geo Pocket",
    "NGP": "SNK Neo Geo Pocket",
    "NGPC": "SNK Neo Geo Pocket Color",
    "LYNX": "Atari Lynx",
    "JAGUAR": "Atari Jaguar",
    "2600": "Atari 2600",
    "ATARI2600": "Atari 2600",
    "WSWAN": "Bandai WonderSwan",
    "WSWANC": "Bandai WonderSwan Color",
    "COLECO": "Coleco ColecoVision",
    "VECTREX": "GCE Vectrex",
}

ROM_EXTS = {
    ".zip", ".7z", ".nes", ".sfc", ".smc", ".gb", ".gbc", ".gba",
    ".nds", ".n64", ".z64", ".v64", ".md", ".smd", ".gen", ".iso",
    ".bin", ".cue", ".img", ".pbp", ".chd", ".pce", ".lnx", ".rom",
    ".col", ".int", ".ws", ".wsc", ".ngp", ".ngc",
}

# ── Manual genre overrides for common games not in OpenVGDB ──────────────────
OVERRIDES = {
    "Adventures of Lolo": "Puzzle",
    "Adventures of Lolo 3": "Puzzle",
    "Kirby's Adventure": "Platformer",
    "Mighty Final Fight": "Beat 'em Up",
    "Ninja Gaiden": "Action",
    "Ninja Gaiden II - The Dark Sword of Chaos (USA)": "Action",
    "Nintendo World Cup": "Sports",
    "Super Dodge Ball": "Sports",
    "Super Mario Bros": "Platformer",
    "Super Mario Bros. 2": "Platformer",
    "Super Mario Bros. 3": "Platformer",
    "The Legend of Zelda": "Action/Adventure",
    "Cave Noire": "RPG",
    "Daedalian Opus": "Puzzle",
    "Donkey Kong": "Platformer",
    "Kwirk: He's A-maze-ing!": "Puzzle",
    "Mole Mania (USA) (SGB Enhanced)": "Puzzle",
    "Noobow": "Puzzle",
    "Pokemon: Blue Version": "RPG",
    "Advance Wars 2 - Black Hole Rising": "Strategy",
    "Castlevania - Circle of the Moon": "Platformer",
    "Mother 3 (Tr)": "RPG",
    "Castlevania - Symphony of the Night": "Platformer",
    "Suikoden II": "RPG",
    "Chrono Trigger": "RPG",
    "EarthBound": "RPG",
    "Secret of Mana": "Action RPG",
    "Super Metroid": "Platformer",
    "The Legend of Zelda: Link's Awakening DX": "Action/Adventure",
    "The Legend of Zelda: Oracle of Ages": "Action/Adventure",
    "The Legend of Zelda: Oracle of Seasons": "Action/Adventure",
}

# ── Database queries ──────────────────────────────────────────────────────────
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


def crc32_of(path: Path) -> str:
    try:
        if path.suffix.lower() == ".zip":
            with zipfile.ZipFile(path) as zf:
                names = zf.namelist()
                if not names:
                    return ""
                data = zf.read(names[0])
        else:
            with open(path, "rb") as f:
                data = f.read(64 * 1024 * 1024)
        return f"{zlib.crc32(data) & 0xFFFFFFFF:08X}"
    except Exception:
        return ""


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


def load_existing(gamelist: Path) -> dict:
    result = {}
    if not gamelist.exists():
        return result
    try:
        tree = ET.parse(gamelist)
        for el in tree.getroot().findall("game"):
            path = (el.findtext("path") or "").lstrip("./")
            result[path] = {
                "path": path,
                "name": el.findtext("name") or path,
                "genre": el.findtext("genre") or "Unsorted",
            }
    except Exception:
        pass
    return result


def write_gamelist(games: list, dest: Path):
    root = ET.Element("gameList")
    for g in sorted(games, key=lambda x: x["name"].lower()):
        el = ET.SubElement(root, "game")
        ET.SubElement(el, "path").text = "./" + g["path"]
        ET.SubElement(el, "name").text = g["name"]
        ET.SubElement(el, "genre").text = g["genre"]
    raw = ET.tostring(root, encoding="unicode")
    pretty = minidom.parseString(raw).toprettyxml(indent="  ", encoding=None)
    dest.write_text(pretty, encoding="utf-8")


def apply_overrides(xml_path: Path) -> int:
    try:
        tree = ET.parse(xml_path)
    except Exception:
        return 0
    root = tree.getroot()
    changed = 0
    for game in root.findall("game"):
        genre_el = game.find("genre")
        if genre_el is None or genre_el.text != "Unsorted":
            continue
        name = game.findtext("name") or ""
        if name in OVERRIDES:
            genre_el.text = OVERRIDES[name]
            changed += 1
    if changed:
        raw = ET.tostring(root, encoding="unicode")
        pretty = minidom.parseString(raw).toprettyxml(indent="  ", encoding=None)
        xml_path.write_text(pretty, encoding="utf-8")
    return changed


# ── GUI ───────────────────────────────────────────────────────────────────────

class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("PocketOS Genre Scanner")
        self.resizable(False, False)
        self.configure(bg="#1e1e2e")

        self._roms_path = tk.StringVar()
        self._db_path = tk.StringVar()
        self._running = False

        self._build_ui()
        self._center()

    def _center(self):
        self.update_idletasks()
        w, h = self.winfo_width(), self.winfo_height()
        x = (self.winfo_screenwidth()  - w) // 2
        y = (self.winfo_screenheight() - h) // 2
        self.geometry(f"+{x}+{y}")

    def _build_ui(self):
        PAD = 16
        FG  = "#cdd6f4"
        BG  = "#1e1e2e"
        ENT = "#313244"
        ACC = "#89b4fa"
        BTN = "#45475a"

        # Title
        tk.Label(self, text="PocketOS Genre Scanner", font=("Helvetica", 16, "bold"),
                 fg=ACC, bg=BG).pack(pady=(PAD, 4))
        tk.Label(self, text="Scans your ROMs and writes miyoogamelist.xml files\nso PocketOS can browse games by genre.",
                 font=("Helvetica", 10), fg="#a6adc8", bg=BG, justify="center").pack(pady=(0, PAD))

        frame = tk.Frame(self, bg=BG, padx=PAD, pady=0)
        frame.pack(fill="x")

        # Roms folder
        tk.Label(frame, text="Roms Folder  (SD card → Roms)", fg=FG, bg=BG,
                 font=("Helvetica", 10, "bold"), anchor="w").grid(row=0, column=0, sticky="w", pady=(0, 2))
        row1 = tk.Frame(frame, bg=BG)
        row1.grid(row=1, column=0, sticky="ew", pady=(0, 10))
        tk.Entry(row1, textvariable=self._roms_path, width=48,
                 bg=ENT, fg=FG, insertbackground=FG, relief="flat",
                 font=("Helvetica", 10)).pack(side="left", fill="x", expand=True, ipady=4)
        tk.Button(row1, text="Browse", command=self._browse_roms,
                  bg=BTN, fg=FG, relief="flat", padx=10, cursor="hand2").pack(side="left", padx=(6, 0))

        # DB file
        tk.Label(frame, text="OpenVGDB Database  (openvgdb.sqlite)", fg=FG, bg=BG,
                 font=("Helvetica", 10, "bold"), anchor="w").grid(row=2, column=0, sticky="w", pady=(0, 2))
        row2 = tk.Frame(frame, bg=BG)
        row2.grid(row=3, column=0, sticky="ew", pady=(0, 4))
        tk.Entry(row2, textvariable=self._db_path, width=48,
                 bg=ENT, fg=FG, insertbackground=FG, relief="flat",
                 font=("Helvetica", 10)).pack(side="left", fill="x", expand=True, ipady=4)
        tk.Button(row2, text="Browse", command=self._browse_db,
                  bg=BTN, fg=FG, relief="flat", padx=10, cursor="hand2").pack(side="left", padx=(6, 0))

        tk.Label(frame, text="Don't have the database?", fg="#a6adc8",
                 bg=BG, font=("Helvetica", 9)).grid(row=4, column=0, sticky="w")
        dl = tk.Label(frame, text="Download openvgdb.sqlite (~170 MB) →",
                      fg=ACC, bg=BG, font=("Helvetica", 9, "underline"), cursor="hand2")
        dl.grid(row=5, column=0, sticky="w", pady=(0, PAD))
        dl.bind("<Button-1>", lambda e: self._download_db())

        frame.columnconfigure(0, weight=1)

        # Buttons
        btnrow = tk.Frame(self, bg=BG, padx=PAD)
        btnrow.pack(fill="x", pady=(0, 8))
        self._scan_btn = tk.Button(btnrow, text="Scan & Generate Game Lists",
                                   command=self._start_scan, bg=ACC, fg="#1e1e2e",
                                   font=("Helvetica", 11, "bold"), relief="flat",
                                   padx=16, pady=8, cursor="hand2")
        self._scan_btn.pack(side="left", fill="x", expand=True)

        # Progress
        self._progress = ttk.Progressbar(self, mode="indeterminate")
        self._progress.pack(fill="x", padx=PAD, pady=(0, 6))

        # Log
        self._log = scrolledtext.ScrolledText(self, height=16, width=70,
                                               bg="#181825", fg=FG,
                                               font=("Courier", 9), relief="flat",
                                               state="disabled")
        self._log.pack(padx=PAD, pady=(0, PAD), fill="both")

        # Status bar
        self._status = tk.Label(self, text="Ready.", fg="#a6adc8", bg="#181825",
                                 font=("Helvetica", 9), anchor="w", padx=8)
        self._status.pack(fill="x", side="bottom")

    def _browse_roms(self):
        d = filedialog.askdirectory(title="Select your Roms folder")
        if d:
            self._roms_path.set(d)

    def _browse_db(self):
        f = filedialog.askopenfilename(title="Select openvgdb.sqlite",
                                        filetypes=[("SQLite database", "*.sqlite *.db"), ("All files", "*")])
        if f:
            self._db_path.set(f)

    def _download_db(self):
        dest = filedialog.asksaveasfilename(
            title="Save openvgdb.sqlite.zip",
            defaultextension=".zip",
            initialfile="openvgdb.sqlite.zip",
            filetypes=[("ZIP file", "*.zip")]
        )
        if not dest:
            return
        self._log_line(f"Downloading OpenVGDB from GitHub...")
        self._progress.start()
        self._scan_btn.config(state="disabled")

        def _do():
            try:
                urllib.request.urlretrieve(OPENVGDB_URL, dest,
                    reporthook=lambda b, bs, t: self._status.config(
                        text=f"Downloading... {min(b*bs, t) // 1024 // 1024} / {t // 1024 // 1024} MB"))
                import zipfile as zf
                self._log_line(f"Extracting {dest}...")
                out_dir = str(Path(dest).parent)
                with zf.ZipFile(dest, 'r') as z:
                    z.extractall(out_dir)
                sqlite_path = str(Path(out_dir) / "openvgdb.sqlite")
                self._db_path.set(sqlite_path)
                self._log_line(f"Database saved to: {sqlite_path}")
                self._status.config(text="Download complete.")
            except Exception as e:
                self._log_line(f"Download failed: {e}")
                self._status.config(text="Download failed.")
            finally:
                self._progress.stop()
                self._scan_btn.config(state="normal")

        threading.Thread(target=_do, daemon=True).start()

    def _log_line(self, text: str):
        def _do():
            self._log.config(state="normal")
            self._log.insert("end", text + "\n")
            self._log.see("end")
            self._log.config(state="disabled")
        self.after(0, _do)

    def _start_scan(self):
        roms = self._roms_path.get().strip()
        db   = self._db_path.get().strip()

        if not roms or not Path(roms).is_dir():
            messagebox.showerror("Error", "Please select a valid Roms folder.")
            return
        if not db or not Path(db).exists():
            messagebox.showerror("Error", "Please select the openvgdb.sqlite database file.")
            return

        self._scan_btn.config(state="disabled")
        self._progress.start()
        self._log.config(state="normal")
        self._log.delete("1.0", "end")
        self._log.config(state="disabled")

        threading.Thread(target=self._scan, args=(Path(roms), Path(db)), daemon=True).start()

    def _scan(self, roms_dir: Path, db_path: Path):
        try:
            conn = sqlite3.connect(str(db_path))
        except Exception as e:
            self._log_line(f"ERROR: Could not open database: {e}")
            self.after(0, self._scan_done)
            return

        total_roms = matched = unsorted = skipped = 0

        for folder in sorted(roms_dir.iterdir()):
            if not folder.is_dir():
                continue
            system_name = SYSTEM_MAP.get(folder.name.upper())
            if not system_name:
                continue

            roms = sorted(p for p in folder.iterdir()
                          if p.is_file() and p.suffix.lower() in ROM_EXTS)
            if not roms:
                continue

            self._log_line(f"\n{folder.name}  ({system_name})  —  {len(roms)} ROMs")
            self.after(0, lambda n=folder.name: self._status.config(text=f"Scanning {n}..."))

            gamelist_path = folder / "miyoogamelist.xml"
            existing = load_existing(gamelist_path)
            games = []

            for rom in roms:
                key = rom.name
                total_roms += 1
                if key in existing and existing[key]["genre"] != "Unsorted":
                    games.append(existing[key])
                    matched += 1
                    continue

                result = db_lookup(conn, rom, system_name)
                if result:
                    title, genre = result
                    games.append({"path": key, "name": title, "genre": genre})
                    self._log_line(f"  ✓  {key[:50]:<50} → {genre}")
                    matched += 1
                else:
                    name = existing[key]["name"] if key in existing else rom.stem
                    games.append({"path": key, "name": name, "genre": "Unsorted"})
                    self._log_line(f"  ✗  {key[:50]:<50} → Unsorted")
                    unsorted += 1

            write_gamelist(games, gamelist_path)

        # Apply manual overrides
        self._log_line("\nApplying manual genre fixes...")
        fixed = 0
        for xml in sorted(roms_dir.glob("*/miyoogamelist.xml")):
            fixed += apply_overrides(xml)
        self._log_line(f"  Fixed {fixed} entries from override list.")

        conn.close()

        self._log_line(f"\n{'─'*55}")
        self._log_line(f"Done!  {total_roms} ROMs scanned")
        self._log_line(f"  Matched:   {matched}")
        self._log_line(f"  Unsorted:  {unsorted - fixed}")
        self._log_line(f"  Fixed:     {fixed}")
        self._log_line(f"\nGame lists written to each system folder in:")
        self._log_line(f"  {roms_dir}")

        self.after(0, self._scan_done)
        self.after(0, lambda: self._status.config(
            text=f"Done — {matched} matched, {unsorted - fixed} still unsorted"))

    def _scan_done(self):
        self._progress.stop()
        self._scan_btn.config(state="normal")


if __name__ == "__main__":
    app = App()
    app.mainloop()
