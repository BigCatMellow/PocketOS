from pathlib import Path


def replace_once(path, old, new):
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    if old not in text:
        raise SystemExit(f"expected text not found in {path}: {old[:100]!r}")
    p.write_text(text.replace(old, new, 1), encoding="utf-8")


# Installer: route ZIP extraction and CRC through the shared safety module.
replace_once(
    "tools/installer.py",
    '''try:\n    from .genre_overrides import load_overrides as load_genre_overrides\nexcept ImportError:  # Direct script and PyInstaller execution.\n    from genre_overrides import load_overrides as load_genre_overrides\n''',
    '''try:\n    from .genre_overrides import load_overrides as load_genre_overrides\nexcept ImportError:  # Direct script and PyInstaller execution.\n    from genre_overrides import load_overrides as load_genre_overrides\n\ntry:\n    from .rom_safety import crc32_of, extract_zip_roms\nexcept ImportError:  # Direct script and PyInstaller execution.\n    from rom_safety import crc32_of, extract_zip_roms\n''',
)

replace_once(
    "tools/installer.py",
    '''def extract_zip(zip_path: Path, dest_folder: Path, log) -> list:\n    extracted = []\n    try:\n        with zipfile.ZipFile(zip_path) as zf:\n            for member in [n for n in zf.namelist() if not n.endswith("/")]:\n                ext = Path(member).suffix.lower()\n                if ext not in ROM_EXTS or _is_doc_file(member):\n                    continue\n                out_path = dest_folder / Path(member).name\n                if out_path.exists():\n                    log(f"    SKIP (exists): {out_path.name}")\n                    continue\n                out_path.write_bytes(zf.read(member))\n                extracted.append(out_path)\n                log(f"    extracted: {out_path.name}")\n    except Exception as e:\n        log(f"    ERROR reading {zip_path.name}: {e}")\n    return extracted\n''',
    '''def extract_zip(zip_path: Path, dest_folder: Path, allowed_extensions, log) -> list:\n    """Use the shared preflighted, streamed, atomic ZIP extractor."""\n    return extract_zip_roms(zip_path, dest_folder, set(allowed_extensions), log)\n''',
)

replace_once(
    "tools/installer.py",
    '''def _crc32_of(path: Path) -> str:\n    try:\n        crc = 0\n        with open(path, "rb") as f:\n            while True:\n                chunk = f.read(1024 * 1024)\n                if not chunk:\n                    break\n                crc = zlib.crc32(chunk, crc)\n        return f"{crc & 0xFFFFFFFF:08X}"\n    except Exception:\n        return ""\n''',
    '''def _crc32_of(path: Path) -> str:\n    return crc32_of(path)\n''',
)

replace_once(
    "tools/installer.py",
    '''                new_files = extract_zip(zip_path, dest_folder, _log)\n''',
    '''                new_files = extract_zip(zip_path, dest_folder, {ext}, _log)\n''',
)

# GUI importer: capture Tk state on the main thread and make the UI/logs match
# the now non-destructive behavior.
replace_once(
    "tools/rom_importer.py",
    '''    def _run(self):\n        self.run_btn.config(state="disabled")\n        self.progress.start()\n        threading.Thread(target=self._import_thread, daemon=True).start()\n''',
    '''    def _run(self):\n        # Tk variables belong to the UI thread; hand the worker a plain bool.\n        self._clean_requested = bool(self.clean_var.get())\n        self.run_btn.config(state="disabled")\n        self.progress.start()\n        threading.Thread(target=self._import_thread, daemon=True).start()\n''',
)

replace_once(
    "tools/rom_importer.py",
    '''        # Variant cleanup (optional)\n        if self.clean_var.get() and affected_systems:\n            self.log("\\n── Removing duplicate/bad/hack variants ──")\n            total_removed = 0\n            for sys_folder in sorted(affected_systems):\n                folder = roms_root / sys_folder\n                removed = clean_variants(folder, self.log)\n                if removed:\n                    self.log(f"  {sys_folder}: removed {removed} variant(s)")\n                    total_removed += removed\n            self.log(f"  total removed: {total_removed}")\n''',
    '''        # Variant analysis (optional, deliberately non-destructive)\n        if getattr(self, "_clean_requested", False) and affected_systems:\n            self.log("\\n── Analyzing possible duplicate/bad/hack variants (no deletion) ──")\n            total_flagged = 0\n            for sys_folder in sorted(affected_systems):\n                folder = roms_root / sys_folder\n                flagged = clean_variants(folder, self.log)\n                if flagged:\n                    self.log(f"  {sys_folder}: flagged {flagged} possible variant(s)")\n                    total_flagged += flagged\n            self.log(f"  total flagged: {total_flagged}; no ROM files changed")\n''',
)

# Functional regression: installer wrapper must use streamed safe extraction and
# must not pull unrelated ROM extensions into the selected system folder.
replace_once(
    "tests/test_installer.py",
    '''import tempfile\nimport unittest\nimport zlib\n''',
    '''import tempfile\nimport unittest\nimport zipfile\nimport zlib\n''',
)
replace_once(
    "tests/test_installer.py",
    '''    POCKETOS_TRANSACTION_PATHS, _crc32_of, _select_candidate, audit_install,\n    clean_variants, detect_onion, install_from_dir, scan_genres_for_system, uninstall,\n''',
    '''    POCKETOS_TRANSACTION_PATHS, _crc32_of, _select_candidate, audit_install,\n    clean_variants, detect_onion, extract_zip, install_from_dir, scan_genres_for_system, uninstall,\n''',
)
marker = '\n\nif __name__ == "__main__":\n'
p = Path("tests/test_installer.py")
text = p.read_text(encoding="utf-8")
if marker not in text:
    raise SystemExit("test marker missing")
insert = r'''
    def test_installer_zip_extraction_is_streamed_and_system_scoped(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            archive = root / "mixed.zip"
            dest = root / "GBA"
            dest.mkdir()
            with zipfile.ZipFile(archive, "w") as zf:
                zf.writestr("Game.gba", b"gba-data")
                zf.writestr("WrongSystem.gb", b"gb-data")

            with patch.object(zipfile.ZipFile, "read", side_effect=AssertionError("must stream")):
                extracted = extract_zip(archive, dest, {".gba"}, lambda _message: None)

            self.assertEqual(["Game.gba"], [path.name for path in extracted])
            self.assertEqual(b"gba-data", (dest / "Game.gba").read_bytes())
            self.assertFalse((dest / "WrongSystem.gb").exists())
'''
text = text.replace(marker, insert + marker, 1)
p.write_text(text, encoding="utf-8")

# Source-level guard for the GUI worker boundary and truthful non-destructive text.
p = Path("tests/test_rom_tool_safety.py")
text = p.read_text(encoding="utf-8")
if marker not in text:
    raise SystemExit("rom safety test marker missing")
insert = r'''

class RomImporterUiSafetyTests(unittest.TestCase):
    def test_variant_analysis_is_truthfully_non_destructive_and_tk_state_is_captured(self):
        source = (Path(__file__).resolve().parents[1] / "tools" / "rom_importer.py").read_text()
        self.assertIn('self._clean_requested = bool(self.clean_var.get())', source)
        worker = source[source.index("    def _do_import(self):"):]
        self.assertNotIn("self.clean_var.get()", worker)
        self.assertIn("Analyzing possible duplicate/bad/hack variants (no deletion)", source)
        self.assertNotIn("Removing duplicate/bad/hack variants", source)
'''
text = text.replace(marker, insert + marker, 1)
p.write_text(text, encoding="utf-8")
