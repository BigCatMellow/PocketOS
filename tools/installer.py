#!/usr/bin/env python3
"""
PocketOS Installer
Installs or removes PocketOS on a Miyoo Mini Plus SD card.
"""

import os
import sys
import shutil
import threading
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

VERSION = "v1.0"


def detect_sd(path: Path) -> bool:
    """Check if this looks like a Miyoo SD card root."""
    return (path / ".tmp_update").is_dir() and (path / "Roms").is_dir()


def install(sd: Path, log):
    bin_dest = sd / ".tmp_update" / "bin"
    res_dest = sd / ".tmp_update" / "res" / "pocketos"

    log("Creating directories...")
    bin_dest.mkdir(parents=True, exist_ok=True)
    res_dest.mkdir(parents=True, exist_ok=True)

    log("Copying binary...")
    shutil.copy2(PAYLOAD_BIN, bin_dest / "pocketOS")

    log("Copying assets...")
    if res_dest.exists():
        shutil.rmtree(res_dest)
    shutil.copytree(PAYLOAD_RES, res_dest)

    log(f"\nPocketOS {VERSION} installed successfully!")
    log("Safely eject your SD card and insert it into your Miyoo Mini Plus.")
    log("PocketOS will launch automatically on boot.")


def uninstall(sd: Path, log):
    target = sd / ".tmp_update" / "bin" / "pocketOS"
    if target.exists():
        target.unlink()
        log("Removed .tmp_update/bin/pocketOS")
    else:
        log("PocketOS binary not found — already uninstalled?")

    res = sd / ".tmp_update" / "res" / "pocketos"
    if res.exists():
        shutil.rmtree(res)
        log("Removed .tmp_update/res/pocketos/")

    log("\nPocketOS uninstalled. Onion OS default menu will return on next boot.")


# ── GUI ───────────────────────────────────────────────────────────────────────

class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title(f"PocketOS Installer {VERSION}")
        self.resizable(False, False)
        self.configure(bg="#1e1e2e")
        self._sd_path = tk.StringVar()
        self._build_ui()
        self._center()
        self._auto_detect()

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

        # Header
        tk.Label(self, text="PocketOS", font=("Helvetica", 22, "bold"),
                 fg=ACC, bg=BG).pack(pady=(PAD, 0))
        tk.Label(self, text=f"Installer  {VERSION}",
                 font=("Helvetica", 11), fg="#a6adc8", bg=BG).pack()
        tk.Label(self, text="A minimal launcher for the Miyoo Mini Plus\nBuilt on top of Onion OS",
                 font=("Helvetica", 10), fg="#6c7086", bg=BG, justify="center").pack(pady=(4, PAD))

        ttk.Separator(self, orient="horizontal").pack(fill="x", padx=PAD)

        # SD card picker
        frame = tk.Frame(self, bg=BG, padx=PAD, pady=PAD)
        frame.pack(fill="x")

        tk.Label(frame, text="SD Card  (select the root of your Miyoo SD card)",
                 fg=FG, bg=BG, font=("Helvetica", 10, "bold"), anchor="w").pack(fill="x")

        row = tk.Frame(frame, bg=BG)
        row.pack(fill="x", pady=(4, 0))
        self._sd_entry = tk.Entry(row, textvariable=self._sd_path, width=52,
                                   bg=ENT, fg=FG, insertbackground=FG, relief="flat",
                                   font=("Helvetica", 10))
        self._sd_entry.pack(side="left", fill="x", expand=True, ipady=4)
        tk.Button(row, text="Browse", command=self._browse,
                  bg=BTN, fg=FG, relief="flat", padx=10, cursor="hand2").pack(side="left", padx=(6, 0))

        self._detect_lbl = tk.Label(frame, text="", fg="#a6adc8", bg=BG, font=("Helvetica", 9))
        self._detect_lbl.pack(anchor="w", pady=(4, 0))

        # Action buttons
        btnframe = tk.Frame(self, bg=BG, padx=PAD)
        btnframe.pack(fill="x", pady=(0, 8))

        self._install_btn = tk.Button(btnframe, text="Install PocketOS",
                                       command=self._do_install,
                                       bg=ACC, fg="#1e1e2e",
                                       font=("Helvetica", 12, "bold"),
                                       relief="flat", padx=16, pady=10, cursor="hand2")
        self._install_btn.pack(side="left", fill="x", expand=True, padx=(0, 6))

        self._remove_btn = tk.Button(btnframe, text="Uninstall",
                                      command=self._do_uninstall,
                                      bg=BTN, fg=RED,
                                      font=("Helvetica", 11),
                                      relief="flat", padx=16, pady=10, cursor="hand2")
        self._remove_btn.pack(side="left")

        # Progress + log
        self._progress = ttk.Progressbar(self, mode="indeterminate")
        self._progress.pack(fill="x", padx=PAD, pady=(0, 6))

        self._log = scrolledtext.ScrolledText(self, height=10, width=64,
                                               bg="#181825", fg=FG,
                                               font=("Courier", 9), relief="flat",
                                               state="disabled")
        self._log.pack(padx=PAD, pady=(0, PAD), fill="both")

        self._status = tk.Label(self, text="Select your SD card to get started.",
                                 fg="#a6adc8", bg="#181825",
                                 font=("Helvetica", 9), anchor="w", padx=8)
        self._status.pack(fill="x", side="bottom")

        self._sd_path.trace_add("write", lambda *_: self._validate())

    def _auto_detect(self):
        """Try to find the SD card automatically."""
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
                        for grandchild in child.iterdir() if child.is_dir() else []:
                            if detect_sd(grandchild):
                                candidates.append(str(grandchild))

        if candidates:
            self._sd_path.set(candidates[0])
            self._detect_lbl.config(text=f"✓ Miyoo SD card detected automatically", fg="#a6e3a1")

    def _browse(self):
        d = filedialog.askdirectory(title="Select the root of your Miyoo SD card")
        if d:
            self._sd_path.set(d)

    def _validate(self):
        p = Path(self._sd_path.get().strip())
        if p.is_dir():
            if detect_sd(p):
                self._detect_lbl.config(text="✓ Looks like a valid Miyoo SD card", fg="#a6e3a1")
            else:
                self._detect_lbl.config(
                    text="⚠ Couldn't confirm this is a Miyoo SD card — make sure you've selected the root",
                    fg="#fab387")

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

    def _do_install(self):
        sd = Path(self._sd_path.get().strip())
        if not sd.is_dir():
            messagebox.showerror("Error", "Please select a valid SD card folder.")
            return

        if not PAYLOAD_BIN.exists():
            messagebox.showerror("Error", f"Installer payload not found.\nExpected: {PAYLOAD_BIN}")
            return

        self._log.config(state="normal")
        self._log.delete("1.0", "end")
        self._log.config(state="disabled")
        self._set_busy(True)
        self._status.config(text="Installing...")

        def _run():
            try:
                install(sd, self._log_line)
                self.after(0, lambda: self._status.config(text="Installation complete!"))
            except Exception as e:
                self._log_line(f"\nERROR: {e}")
                self.after(0, lambda: self._status.config(text="Installation failed."))
            finally:
                self.after(0, lambda: self._set_busy(False))

        threading.Thread(target=_run, daemon=True).start()

    def _do_uninstall(self):
        sd = Path(self._sd_path.get().strip())
        if not sd.is_dir():
            messagebox.showerror("Error", "Please select a valid SD card folder.")
            return
        if not messagebox.askyesno("Uninstall PocketOS",
                                    "This will remove PocketOS and restore the default Onion OS menu.\n\nContinue?"):
            return

        self._log.config(state="normal")
        self._log.delete("1.0", "end")
        self._log.config(state="disabled")
        self._set_busy(True)
        self._status.config(text="Uninstalling...")

        def _run():
            try:
                uninstall(sd, self._log_line)
                self.after(0, lambda: self._status.config(text="Uninstalled."))
            except Exception as e:
                self._log_line(f"\nERROR: {e}")
                self.after(0, lambda: self._status.config(text="Uninstall failed."))
            finally:
                self.after(0, lambda: self._set_busy(False))

        threading.Thread(target=_run, daemon=True).start()


if __name__ == "__main__":
    app = App()
    app.mainloop()
