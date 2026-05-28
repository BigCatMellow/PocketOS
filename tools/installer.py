#!/usr/bin/env python3
"""
PocketOS Installer
Installs or removes PocketOS on a Miyoo Mini Plus SD card.
"""

import os
import sys
import json
import shutil
import threading
import subprocess
import tempfile
import urllib.request
import urllib.error
import webbrowser
import zipfile
import tkinter as tk
from tkinter import ttk, filedialog, scrolledtext, messagebox
from pathlib import Path

# ── Bundled assets path (works both running as script and PyInstaller exe) ───
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


# ── Helpers ───────────────────────────────────────────────────────────────────

def detect_sd(path: Path) -> bool:
    return (path / ".tmp_update").is_dir() and (path / "Roms").is_dir()


def detect_onion(path: Path) -> bool:
    return (path / "miyoo" / "app" / "MainUI").exists() or \
           (path / ".tmp_update" / "onion_version").exists() or \
           (path / "BIOS").is_dir()


def roms_missing_gamelists(sd: Path) -> list:
    roms_dir = sd / "Roms"
    missing = []
    if not roms_dir.is_dir():
        return missing
    for system in sorted(roms_dir.iterdir()):
        if not system.is_dir():
            continue
        has_roms = any(
            f.suffix.lower() not in {".xml", ".db", ".txt", ""}
            for f in system.iterdir() if f.is_file()
        )
        if has_roms and not (system / "miyoogamelist.xml").exists():
            missing.append(system.name)
    return missing


def fetch_latest_release():
    """Return (tag, zip_url) from GitHub, or (None, None) on failure."""
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
             if a["name"].endswith(".zip") and "pocketOS" in a["name"]),
            None
        )
        return tag, zip_url
    except Exception:
        return None, None


def version_tuple(v: str):
    """Turn 'v1.2' into (1, 2) for comparison."""
    return tuple(int(x) for x in v.lstrip("v").split(".") if x.isdigit())


def install_from_dir(src: Path, sd: Path, log):
    """Install pocketOS binary + assets from an extracted release directory."""
    bin_src  = src / ".tmp_update" / "bin" / "pocketOS"
    res_src  = src / ".tmp_update" / "res" / "pocketos"
    bin_dest = sd  / ".tmp_update" / "bin"
    res_dest = sd  / ".tmp_update" / "res" / "pocketos"

    if not bin_src.exists():
        raise FileNotFoundError(f"Binary not found in download: {bin_src}")

    log("► Setting up folders on your SD card...")
    bin_dest.mkdir(parents=True, exist_ok=True)
    res_dest.mkdir(parents=True, exist_ok=True)

    log("► Copying the PocketOS launcher...")
    shutil.copy2(bin_src, bin_dest / "pocketOS")

    log("► Copying themes, icons, and fonts...")
    if res_dest.exists():
        shutil.rmtree(res_dest)
    shutil.copytree(res_src, res_dest)


def install(sd: Path, log):
    install_from_dir(BASE_DIR, sd, log)
    _install_success_log(log, VERSION)


def _install_success_log(log, version):
    log("")
    log(f"✓ PocketOS {version} installed successfully!")
    log("")
    log("What to do next:")
    log("  1. Close this window")
    log("  2. Safely eject your SD card")
    log("  3. Insert it into your Miyoo Mini Plus and power on")
    log("  4. PocketOS will launch automatically — no extra steps needed")
    log("")
    log("Your games, saves, and Onion settings are untouched.")


def uninstall(sd: Path, log):
    log("► Removing PocketOS launcher...")
    target = sd / ".tmp_update" / "bin" / "pocketOS"
    if target.exists():
        target.unlink()
        log("  Removed launcher binary")
    else:
        log("  PocketOS binary not found — may already be uninstalled")

    log("► Removing PocketOS themes and assets...")
    res = sd / ".tmp_update" / "res" / "pocketos"
    if res.exists():
        shutil.rmtree(res)
        log("  Removed themes, icons, and fonts")

    log("")
    log("✓ PocketOS removed.")
    log("")
    log("What to do next:")
    log("  1. Safely eject your SD card")
    log("  2. Insert it into your Miyoo Mini Plus and power on")
    log("  3. The default Onion OS menu will return automatically")
    log("")
    log("Your games and saves are untouched.")


# ── GUI ───────────────────────────────────────────────────────────────────────

class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title(f"PocketOS Installer {VERSION}")
        self.resizable(False, False)
        self.configure(bg="#1e1e2e")
        self._sd_path    = tk.StringVar()
        self._latest_tag = None
        self._latest_url = None
        self._build_ui()
        self._center()
        self._auto_detect()
        self._check_for_update()

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
        RED = "#f38ba8"
        SUB = "#a6adc8"
        DIM = "#6c7086"

        # ── ASCII logo ────────────────────────────────────────────────────────
        LOGO = (
            r" ____             _        _    ___  ____  " + "\n"
            r"|  _ \ ___   ___ | | _____| |_ / _ \/ ___| " + "\n"
            r"| |_) / _ \ / __|| |/ / _ \ __| | | \___ \ " + "\n"
            r"|  __/ (_) | (__ |   <  __/ |_| |_| |___) |" + "\n"
            r"|_|   \___/ \___||_|\_\___|\__|\___/|____/ " + "\n"
            f"                       Installer  {VERSION}  "
        )
        tk.Label(self, text=LOGO, font=("Courier", 9, "bold"),
                 fg=ACC, bg=BG, justify="center").pack(pady=(PAD, 4))
        tk.Label(self, text="A minimal launcher for the Miyoo Mini Plus  ·  Built on Onion OS",
                 font=("Helvetica", 9), fg=DIM, bg=BG, justify="center").pack(pady=(0, 8))

        # ── Update banner (hidden until version check completes) ──────────────
        self._update_frame = tk.Frame(self, bg="#1e3a5f", padx=PAD, pady=8)
        self._update_lbl = tk.Label(self._update_frame, text="", fg="#89dceb",
                                     bg="#1e3a5f", font=("Helvetica", 9), anchor="w",
                                     justify="left")
        self._update_lbl.pack(side="left", fill="x", expand=True)
        self._update_btn = tk.Button(self._update_frame, text="",
                                      command=self._do_update_install,
                                      bg="#45475a", fg="#a6e3a1", relief="flat",
                                      padx=8, cursor="hand2", font=("Helvetica", 9, "bold"))
        self._update_btn.pack(side="right")
        self._update_frame.pack_forget()

        ttk.Separator(self, orient="horizontal").pack(fill="x", padx=PAD)

        # ── How to use ────────────────────────────────────────────────────────
        steps_frame = tk.Frame(self, bg="#181825", padx=PAD, pady=10)
        steps_frame.pack(fill="x", padx=PAD, pady=(10, 0))
        tk.Label(steps_frame, text="How to install", font=("Helvetica", 9, "bold"),
                 fg=ACC, bg="#181825", anchor="w").pack(fill="x")
        steps = [
            ("1", "Insert your Miyoo Mini Plus SD card into your computer."),
            ("2", "Select the SD card root folder below  (it contains Roms/, BIOS/, etc.)"),
            ("3", "Click  Install PocketOS  and wait for it to finish."),
            ("4", "Eject the SD card safely, insert it into your device, and power on."),
            ("",  "PocketOS launches automatically — no extra steps needed on the device."),
        ]
        for num, text in steps:
            row = tk.Frame(steps_frame, bg="#181825")
            row.pack(fill="x", pady=1)
            tk.Label(row, text=f" {num}. " if num else "    ",
                     font=("Helvetica", 9, "bold") if num else ("Helvetica", 9),
                     fg=ACC, bg="#181825", width=3, anchor="e").pack(side="left")
            tk.Label(row, text=text, font=("Helvetica", 9),
                     fg=SUB, bg="#181825", anchor="w").pack(side="left")

        ttk.Separator(self, orient="horizontal").pack(fill="x", padx=PAD, pady=(10, 0))

        # ── SD card picker ────────────────────────────────────────────────────
        frame = tk.Frame(self, bg=BG, padx=PAD, pady=PAD)
        frame.pack(fill="x")
        tk.Label(frame, text="Step 1 — Select your SD card",
                 fg=FG, bg=BG, font=("Helvetica", 10, "bold"), anchor="w").pack(fill="x")
        tk.Label(frame,
                 text="Browse to the root of the card — the folder that contains Roms/ and BIOS/.",
                 fg=DIM, bg=BG, font=("Helvetica", 9), anchor="w").pack(fill="x", pady=(2, 4))
        row = tk.Frame(frame, bg=BG)
        row.pack(fill="x")
        self._sd_entry = tk.Entry(row, textvariable=self._sd_path, width=52,
                                   bg=ENT, fg=FG, insertbackground=FG, relief="flat",
                                   font=("Helvetica", 10))
        self._sd_entry.pack(side="left", fill="x", expand=True, ipady=4)
        tk.Button(row, text="Browse…", command=self._browse,
                  bg=BTN, fg=FG, relief="flat", padx=10, cursor="hand2").pack(side="left", padx=(6, 0))
        self._detect_lbl = tk.Label(frame, text="", fg=SUB, bg=BG, font=("Helvetica", 9))
        self._detect_lbl.pack(anchor="w", pady=(4, 0))

        # ── Onion OS warning banner ───────────────────────────────────────────
        self._onion_frame = tk.Frame(self, bg="#313244", padx=PAD, pady=8)
        self._onion_lbl = tk.Label(self._onion_frame, text="", fg="#fab387", bg="#313244",
                                    font=("Helvetica", 9), justify="left", anchor="w",
                                    wraplength=380)
        self._onion_lbl.pack(side="left", fill="x", expand=True)
        tk.Button(self._onion_frame, text="Get Onion OS →",
                  command=lambda: webbrowser.open(ONION_URL),
                  bg="#45475a", fg="#89b4fa", relief="flat",
                  padx=8, cursor="hand2", font=("Helvetica", 9)).pack(side="right")
        self._onion_frame.pack_forget()

        # ── Action buttons ────────────────────────────────────────────────────
        tk.Label(self, text="Step 2 — Install or remove PocketOS",
                 fg=FG, bg=BG, font=("Helvetica", 10, "bold"),
                 anchor="w", padx=PAD).pack(fill="x")
        btnframe = tk.Frame(self, bg=BG, padx=PAD)
        btnframe.pack(fill="x", pady=(6, 4))
        self._install_btn = tk.Button(btnframe, text="⬇  Install PocketOS",
                                       command=self._do_install,
                                       bg=ACC, fg="#1e1e2e",
                                       font=("Helvetica", 12, "bold"),
                                       relief="flat", padx=16, pady=10, cursor="hand2")
        self._install_btn.pack(side="left", fill="x", expand=True, padx=(0, 6))
        self._remove_btn = tk.Button(btnframe, text="Remove",
                                      command=self._do_uninstall,
                                      bg=BTN, fg=RED, font=("Helvetica", 11),
                                      relief="flat", padx=16, pady=10, cursor="hand2")
        self._remove_btn.pack(side="left")
        tk.Label(self,
                 text="Install copies the launcher to your SD card.  Remove restores the default Onion menu.",
                 fg=DIM, bg=BG, font=("Helvetica", 8), anchor="w", padx=PAD).pack(fill="x")

        # ── Progress + log ────────────────────────────────────────────────────
        self._progress = ttk.Progressbar(self, mode="indeterminate")
        self._progress.pack(fill="x", padx=PAD, pady=(8, 4))
        tk.Label(self, text="Progress log", fg=DIM, bg=BG,
                 font=("Helvetica", 8), anchor="w", padx=PAD).pack(fill="x")
        self._log = scrolledtext.ScrolledText(self, height=9, width=64,
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

    def _show_update_banner(self, tag: str, url: str):
        self._latest_tag = tag
        self._latest_url = url
        self._update_lbl.config(
            text=f"★  A newer version is available: {tag}\n"
                 f"   Click to download and install {tag} instead of the bundled {VERSION}."
        )
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
                if mount.exists():
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
            self._status.config(text="SD card found automatically. Click Install PocketOS when ready.")

    def _browse(self):
        d = filedialog.askdirectory(title="Select the root of your Miyoo SD card")
        if d:
            self._sd_path.set(d)

    def _validate(self):
        p = Path(self._sd_path.get().strip())
        if not p.is_dir():
            self._onion_frame.pack_forget()
            return
        if detect_sd(p):
            self._detect_lbl.config(
                text="✓ Looks like a valid Miyoo SD card — ready to install", fg="#a6e3a1")
            self._status.config(text="SD card selected. Click Install PocketOS when ready.")
        else:
            self._detect_lbl.config(
                text="⚠  This doesn't look like the SD card root — select the top-level folder, not a subfolder",
                fg="#fab387")
            self._status.config(text="Wrong folder — select the root of the SD card.")

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
        self._install_btn.config(state=state)
        self._remove_btn.config(state=state)
        if busy:
            self._progress.start()
        else:
            self._progress.stop()

    # ── Install (bundled) ─────────────────────────────────────────────────────

    def _do_install(self):
        sd = self._get_sd()
        if sd is None:
            return
        if not PAYLOAD_BIN.exists():
            messagebox.showerror("Installer Error",
                                 f"PocketOS payload not found inside the installer.\n\n"
                                 f"Try re-downloading from the releases page.\n\n"
                                 f"Expected: {PAYLOAD_BIN}")
            return
        self._clear_log()
        self._set_busy(True)
        self._status.config(text="Installing — please wait, don't eject the SD card...")

        def _run():
            try:
                install(sd, self._log_line)
                self.after(0, lambda: self._status.config(
                    text="✓ Done! Eject your SD card safely, then power on your device."))
                self.after(0, lambda: self._offer_genre_scan(sd))
            except Exception as e:
                self._log_line(f"\n✗ ERROR: {e}")
                self.after(0, lambda: self._status.config(
                    text="Installation failed — check the log above for details."))
            finally:
                self.after(0, lambda: self._set_busy(False))

        threading.Thread(target=_run, daemon=True).start()

    # ── Install (download latest from GitHub) ─────────────────────────────────

    def _do_update_install(self):
        sd = self._get_sd()
        if sd is None:
            return
        if not self._latest_url:
            return
        self._clear_log()
        self._set_busy(True)
        tag = self._latest_tag
        url = self._latest_url
        self._status.config(text=f"Downloading {tag} — please wait, don't eject the SD card...")

        def _run():
            tmp_dir = None
            try:
                self._log_line(f"► Downloading PocketOS {tag} from GitHub...")

                tmp_dir = tempfile.mkdtemp(prefix="pocketos_")
                zip_path = Path(tmp_dir) / f"pocketOS-{tag}.zip"

                def _progress(count, block, total):
                    if total > 0:
                        pct = min(100, count * block * 100 // total)
                        mb  = count * block / 1_048_576
                        tot = total / 1_048_576
                        self._log_line(f"  {pct}%  ({mb:.1f} / {tot:.1f} MB)")

                urllib.request.urlretrieve(url, zip_path, reporthook=_progress)
                self._log_line(f"► Download complete. Extracting...")

                extract_dir = Path(tmp_dir) / "extracted"
                with zipfile.ZipFile(zip_path, "r") as zf:
                    zf.extractall(extract_dir)

                self._log_line(f"► Installing PocketOS {tag} to your SD card...")
                install_from_dir(extract_dir, sd, self._log_line)
                _install_success_log(self._log_line, tag)

                self.after(0, lambda: self._status.config(
                    text=f"✓ PocketOS {tag} installed! Eject your SD card safely, then power on."))
                self.after(0, lambda: self._update_frame.pack_forget())
                self.after(0, lambda: self._offer_genre_scan(sd))
            except Exception as e:
                self._log_line(f"\n✗ ERROR: {e}")
                self.after(0, lambda: self._status.config(
                    text="Download/install failed — check the log above for details."))
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
        self._status.config(text="Removing PocketOS — please wait, don't eject the SD card...")

        def _run():
            try:
                uninstall(sd, self._log_line)
                self.after(0, lambda: self._status.config(
                    text="✓ Done! Eject your SD card safely, then power on your device."))
            except Exception as e:
                self._log_line(f"\n✗ ERROR: {e}")
                self.after(0, lambda: self._status.config(
                    text="Removal failed — check the log above for details."))
            finally:
                self.after(0, lambda: self._set_busy(False))

        threading.Thread(target=_run, daemon=True).start()

    # ── Genre scan offer ──────────────────────────────────────────────────────

    def _offer_genre_scan(self, sd: Path):
        missing = roms_missing_gamelists(sd)
        if not missing:
            return
        systems_str = "\n  • ".join(missing[:8])
        if len(missing) > 8:
            systems_str += f"\n  • … and {len(missing) - 8} more"
        answer = messagebox.askyesno(
            "Enable Browse by Genre?",
            f"PocketOS is installed!\n\n"
            f"Found {len(missing)} system(s) with ROMs but no genre data:\n\n"
            f"  • {systems_str}\n\n"
            f"The Genre Scanner reads your ROM files and automatically sorts\n"
            f"them by genre so Browse by Genre works in PocketOS.\n\n"
            f"Run the Genre Scanner now?"
        )
        if not answer:
            return
        scanner = self._find_genre_scanner()
        if scanner:
            subprocess.Popen([str(scanner)], close_fds=True)
        else:
            messagebox.showinfo(
                "Genre Scanner Not Found",
                "Couldn't find the Genre Scanner next to this installer.\n\n"
                "Download  PocketOS-GenreScanner  for your platform from\n"
                "the same releases page and run it separately.\n\n"
                "Point it at the same SD card and it will set everything up."
            )

    def _find_genre_scanner(self) -> Path | None:
        base = Path(sys.executable).parent if getattr(sys, "frozen", False) else Path(__file__).parent
        for name in ["PocketOS-GenreScanner-linux", "PocketOS-GenreScanner-macos",
                     "PocketOS-GenreScanner-windows.exe"]:
            p = base / name
            if p.exists():
                return p
        p = base / "genre_scanner.py"
        return p if p.exists() else None

    # ── Shared helpers ────────────────────────────────────────────────────────

    def _get_sd(self) -> Path | None:
        sd = Path(self._sd_path.get().strip())
        if not sd.is_dir():
            messagebox.showerror("No SD Card Selected",
                                 "Please select the root folder of your Miyoo SD card first.\n\n"
                                 "It's the top-level folder that contains Roms/, BIOS/, etc.")
            return None
        return sd

    def _clear_log(self):
        self._log.config(state="normal")
        self._log.delete("1.0", "end")
        self._log.config(state="disabled")


if __name__ == "__main__":
    app = App()
    app.mainloop()
