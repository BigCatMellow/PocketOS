#!/usr/bin/env python3
"""
PocketOS Genre Scanner
Scans your Miyoo Mini Plus ROM library against OpenVGDB and writes
miyoogamelist.xml files so PocketOS can browse games by genre.
"""

import os
import sys
import sqlite3
import threading
import xml.etree.ElementTree as ET
from pathlib import Path
import tkinter as tk
from tkinter import ttk, filedialog, scrolledtext, messagebox
import urllib.request

try:
    from .onion_systems import ROM_EXTENSIONS, openvgdb_system_name
    from .genre_overrides import load_overrides as load_genre_overrides
    from .rom_safety import (
        GamelistError, crc32_of, extract_zip_roms, index_games,
        load_gamelist_tree, write_xml_atomic,
    )
except ImportError:  # Direct script and PyInstaller execution.
    from onion_systems import ROM_EXTENSIONS, openvgdb_system_name
    from genre_overrides import load_overrides as load_genre_overrides
    from rom_safety import (
        GamelistError, crc32_of, extract_zip_roms, index_games,
        load_gamelist_tree, write_xml_atomic,
    )

# ── OpenVGDB download URL (latest release asset) ─────────────────────────────
OPENVGDB_URL = "https://github.com/OpenVGDB/OpenVGDB/releases/download/v29.0/openvgdb.sqlite.zip"

# ── Onion ROM/system contract ─────────────────────────────────────────────────
ROM_EXTS = set(ROM_EXTENSIONS)

# ── Manual genre overrides are data, never executable code ────────────────────
OVERRIDES = load_genre_overrides()

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


def apply_overrides(xml_path: Path) -> int:
    try:
        tree = load_gamelist_tree(xml_path)
    except GamelistError:
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
        write_xml_atomic(tree, xml_path)
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
            def set_status(message):
                self.after(0, lambda m=message: self._status.config(text=m))
            try:
                def reporthook(blocks, block_size, total):
                    downloaded = min(blocks * block_size, total) if total > 0 else blocks * block_size
                    total_mb = total // 1024 // 1024 if total > 0 else 0
                    set_status(f"Downloading... {downloaded // 1024 // 1024} / {total_mb} MB")
                urllib.request.urlretrieve(OPENVGDB_URL, dest, reporthook=reporthook)
                self._log_line(f"Extracting {dest}...")
                out_dir = Path(dest).parent
                extracted = extract_zip_roms(Path(dest), out_dir, {".sqlite"}, self._log_line)
                sqlite_files = [path for path in extracted if path.name == "openvgdb.sqlite"]
                sqlite_path = sqlite_files[0] if sqlite_files else out_dir / "openvgdb.sqlite"
                if not sqlite_path.is_file():
                    raise RuntimeError("download archive did not contain openvgdb.sqlite")
                self.after(0, lambda p=str(sqlite_path): self._db_path.set(p))
                self._log_line(f"Database saved to: {sqlite_path}")
                set_status("Download complete.")
            except Exception as e:
                self._log_line(f"Download failed: {e}")
                set_status("Download failed.")
            finally:
                self.after(0, self._progress.stop)
                self.after(0, lambda: self._scan_btn.config(state="normal"))

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
            system_name = openvgdb_system_name(folder.name)
            if not system_name:
                continue

            roms = sorted(p for p in folder.iterdir()
                          if p.is_file() and p.suffix.lower() in ROM_EXTS)
            if not roms:
                continue

            self._log_line(f"\n{folder.name}  ({system_name})  —  {len(roms)} ROMs")
            self.after(0, lambda n=folder.name: self._status.config(text=f"Scanning {n}..."))

            gamelist_path = folder / "miyoogamelist.xml"
            try:
                tree = load_gamelist_tree(gamelist_path)
            except GamelistError as exc:
                self._log_line(f"  ERROR: {exc}")
                skipped += len(roms)
                continue
            root = tree.getroot()
            existing = index_games(root)

            for rom in roms:
                key = rom.name
                total_roms += 1
                game = existing.get(key)
                if game is not None and (game.findtext("genre") or "Unsorted") != "Unsorted":
                    matched += 1
                    continue

                rom_system_name = openvgdb_system_name(folder.name, rom.suffix) or system_name
                result = db_lookup(conn, rom, rom_system_name)
                if result:
                    title, genre = result
                    matched += 1
                    self._log_line(f"  ✓  {key[:50]:<50} → {genre}")
                else:
                    title = (game.findtext("name") if game is not None else None) or rom.stem
                    genre = "Unsorted"
                    unsorted += 1
                    self._log_line(f"  ✗  {key[:50]:<50} → Unsorted")

                if game is None:
                    game = ET.SubElement(root, "game")
                    ET.SubElement(game, "path").text = "./" + key
                    ET.SubElement(game, "name").text = title
                    ET.SubElement(game, "genre").text = genre
                    existing[key] = game
                else:
                    genre_el = game.find("genre")
                    if genre_el is None:
                        genre_el = ET.SubElement(game, "genre")
                    genre_el.text = genre

            write_xml_atomic(tree, gamelist_path)

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
