from pathlib import Path
import re


def replace_once(path, old, new):
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    if old not in text:
        raise SystemExit(f"expected text not found in {path}: {old[:120]!r}")
    p.write_text(text.replace(old, new, 1), encoding="utf-8")


def sub_once(path, pattern, replacement):
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    text, count = re.subn(pattern, replacement, text, count=1, flags=re.S)
    if count != 1:
        raise SystemExit(f"pattern failed in {path}: {pattern[:120]!r}")
    p.write_text(text, encoding="utf-8")


# Registry exposes only the extensions valid for an already-selected target.
p = Path("tools/onion_systems.py")
text = p.read_text(encoding="utf-8")
anchor = '''def openvgdb_system_name(folder: str, rom_suffix: str | None = None) -> str | None:
'''
insert = '''def extensions_for_folder(folder: str) -> frozenset[str]:
    """ROM extensions valid for an explicit/installed target folder."""
    canonical = canonical_folder(folder)
    info = SYSTEMS.get(canonical)
    if info:
        return frozenset(info["extensions"])
    if canonical == "N64":
        return frozenset({".n64", ".z64", ".v64"})
    return frozenset()


'''
if anchor not in text:
    raise SystemExit("onion_systems anchor missing")
p.write_text(text.replace(anchor, insert + anchor, 1), encoding="utf-8")

# Installer: data-only genre overrides; no executable local Python.
p = Path("tools/installer.py")
text = p.read_text(encoding="utf-8")
anchor = '''except ImportError:  # Direct script and PyInstaller execution.
    from onion_systems import ROM_EXTENSIONS, candidates_for_extension, openvgdb_system_name
'''
addition = '''
try:
    from .genre_overrides import load_overrides as load_genre_overrides
except ImportError:  # Direct script and PyInstaller execution.
    from genre_overrides import load_overrides as load_genre_overrides
'''
if anchor not in text:
    raise SystemExit("installer registry import anchor missing")
text = text.replace(anchor, anchor + addition, 1)
text, count = re.subn(
    r'''def _load_overrides\(\) -> dict:\n.*?\n    return ns\.get\("OVERRIDES", \{\}\)\n''',
    '''def _load_overrides() -> dict:
    return load_genre_overrides()
''',
    text,
    count=1,
    flags=re.S,
)
if count != 1:
    raise SystemExit("installer override loader replacement failed")
p.write_text(text, encoding="utf-8")

# Standalone importer: shared CRC/XML/extraction safety, data-only overrides,
# and analysis-only variant reporting.
p = Path("tools/rom_importer.py")
text = p.read_text(encoding="utf-8")
text = text.replace(
    '''try:
    from .onion_systems import ROM_EXTENSIONS, candidates_for_extension, openvgdb_system_name
except ImportError:  # Direct script and PyInstaller execution.
    from onion_systems import ROM_EXTENSIONS, candidates_for_extension, openvgdb_system_name
''',
    '''try:
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
''',
    1,
)
text, count = re.subn(
    r'''def crc32_of\(path: Path\) -> str:\n.*?\n\ndef db_lookup''',
    '''def db_lookup''',
    text,
    count=1,
    flags=re.S,
)
if count != 1:
    raise SystemExit("rom importer crc removal failed")
text, count = re.subn(
    r'''def load_existing\(gamelist: Path\) -> dict:\n.*?\n\n\n# ── Core importer logic''',
    '''# ── Core importer logic''',
    text,
    count=1,
    flags=re.S,
)
if count != 1:
    raise SystemExit("rom importer gamelist helper removal failed")
text, count = re.subn(
    r'''def extract_zip\(zip_path: Path, dest_folder: Path, log\) -> list\[Path\]:\n.*?\n    return extracted\n''',
    '''def extract_zip(zip_path: Path, dest_folder: Path, log) -> list[Path]:
    allowed = extensions_for_folder(dest_folder.name)
    if not allowed:
        log(f"  ERROR: no safe extraction extension contract for {dest_folder.name}")
        return []
    return extract_zip_roms(zip_path, dest_folder, allowed, log)
''',
    text,
    count=1,
    flags=re.S,
)
if count != 1:
    raise SystemExit("rom importer extract replacement failed")
text, count = re.subn(
    r'''def scan_genres_for_system\(roms_root: Path, system_folder: str,\n                           db_path: Path, log\) -> int:\n.*?\n    return added\n''',
    '''def scan_genres_for_system(roms_root: Path, system_folder: str,
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
''',
    text,
    count=1,
    flags=re.S,
)
if count != 1:
    raise SystemExit("rom importer scanner replacement failed")
text = text.replace(
    '''    if changed:
        raw    = ET.tostring(root, encoding="unicode")
        pretty = minidom.parseString(raw).toprettyxml(indent="  ", encoding=None)
        gamelist.write_text(pretty, encoding="utf-8")
        log(f"  overrides: {system_folder} — fixed {changed} game(s)")
''',
    '''    if changed:
        write_xml_atomic(tree, gamelist)
        log(f"  overrides: {system_folder} — fixed {changed} game(s)")
''',
    1,
)
text, count = re.subn(
    r'''def clean_variants\(folder: Path, log\) -> int:\n.*?\n    return len\(to_remove\)\n''',
    '''def clean_variants(folder: Path, log) -> int:
    """Report possible variants without modifying user ROM files."""
    to_remove, to_keep = find_variants_to_remove(folder)
    if not to_remove:
        return 0
    if to_keep:
        log(f"  suggested keep → {to_keep[0].name}")
    for path in to_remove:
        log(f"  possible variant (no action) → {path.name}")
    return len(to_remove)
''',
    text,
    count=1,
    flags=re.S,
)
if count != 1:
    raise SystemExit("rom importer variant replacement failed")
text = text.replace(
    'tk.Checkbutton(self, text="Remove duplicate/bad/hack variants (keep best dump per game)",\n',
    'tk.Checkbutton(self, text="Analyze possible duplicate/bad/hack variants (no deletion)",\n',
    1,
)
text, count = re.subn(
    r'''        # Find fix_unsorted overrides\n        overrides = \{\}\n        fix_path = Path\(__file__\)\.parent / "fix_unsorted\.py"\n        if fix_path\.exists\(\):\n            ns = \{\}\n            exec\(fix_path\.read_text\(\), ns\)\n            overrides = ns\.get\("OVERRIDES", \{\}\)\n''',
    '''        # Data-only manual genre overrides.
        overrides = load_genre_overrides()
''',
    text,
    count=1,
    flags=re.S,
)
if count != 1:
    raise SystemExit("rom importer override exec replacement failed")
text = text.replace(
    '''        # Variant cleanup (optional)
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
''',
    '''        # Variant analysis (optional, never destructive).
        if self.clean_var.get() and affected_systems:
            self.log("\n── Analyzing possible duplicate/bad/hack variants (no deletion) ──")
            total_flagged = 0
            for sys_folder in sorted(affected_systems):
                folder = roms_root / sys_folder
                flagged = clean_variants(folder, self.log)
                if flagged:
                    self.log(f"  {sys_folder}: flagged {flagged} possible variant(s)")
                    total_flagged += flagged
            self.log(f"  total flagged: {total_flagged}; no ROM files were changed")
''',
    1,
)
p.write_text(text, encoding="utf-8")

# Genre scanner: shared safe CRC/XML operations and UI-thread-only Tk changes.
p = Path("tools/genre_scanner.py")
text = p.read_text(encoding="utf-8")
text = text.replace(
    '''try:
    from .onion_systems import ROM_EXTENSIONS, candidates_for_extension, openvgdb_system_name
except ImportError:  # Direct script and PyInstaller execution.
    from onion_systems import ROM_EXTENSIONS, candidates_for_extension, openvgdb_system_name
''',
    '''try:
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
''',
    1,
)
text, count = re.subn(
    r'''# ── Manual genre overrides for common games not in OpenVGDB ─+\nOVERRIDES = \{.*?\n\}\n''',
    '''# ── Manual genre overrides are data, never executable code ────────────────────
OVERRIDES = load_genre_overrides()
''',
    text,
    count=1,
    flags=re.S,
)
if count != 1:
    raise SystemExit("genre scanner override map removal failed")
text, count = re.subn(
    r'''def crc32_of\(path: Path\) -> str:\n.*?\n\ndef db_lookup''',
    '''def db_lookup''',
    text,
    count=1,
    flags=re.S,
)
if count != 1:
    raise SystemExit("genre scanner crc removal failed")
text, count = re.subn(
    r'''def load_existing\(gamelist: Path\) -> dict:\n.*?\n\ndef apply_overrides''',
    '''def apply_overrides''',
    text,
    count=1,
    flags=re.S,
)
if count != 1:
    raise SystemExit("genre scanner gamelist helper removal failed")
text, count = re.subn(
    r'''def apply_overrides\(xml_path: Path\) -> int:\n.*?\n    return changed\n''',
    '''def apply_overrides(xml_path: Path) -> int:
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
''',
    text,
    count=1,
    flags=re.S,
)
if count != 1:
    raise SystemExit("genre scanner override function replacement failed")
# Download worker: all Tk mutations return to the main thread and ZIP extraction
# is limited to the expected sqlite member via the shared safe extractor.
old = '''        def _do():
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
'''
new = '''        def _do():
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
'''
if old not in text:
    raise SystemExit("genre scanner download worker block missing")
text = text.replace(old, new, 1)
# Rewrite per-folder scan so existing XML nodes are mutated in place.
old = '''            gamelist_path = folder / "miyoogamelist.xml"
            existing = load_existing(gamelist_path)
            games = []

            for rom in roms:
                key = rom.name
                total_roms += 1
                if key in existing and existing[key]["genre"] != "Unsorted":
                    games.append(existing[key])
                    matched += 1
                    continue

                rom_system_name = openvgdb_system_name(folder.name, rom.suffix) or system_name
                result = db_lookup(conn, rom, rom_system_name)
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
'''
new = '''            gamelist_path = folder / "miyoogamelist.xml"
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
                    name_el = game.find("name")
                    if name_el is None:
                        name_el = ET.SubElement(game, "name")
                    name_el.text = title
                    genre_el = game.find("genre")
                    if genre_el is None:
                        genre_el = ET.SubElement(game, "genre")
                    genre_el.text = genre

            write_xml_atomic(tree, gamelist_path)
'''
if old not in text:
    raise SystemExit("genre scanner scan block missing")
text = text.replace(old, new, 1)
p.write_text(text, encoding="utf-8")

# Tests exercise the actual destructive/resource boundaries.
Path("tests/test_rom_tool_safety.py").write_text(r'''import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch

from tools.genre_overrides import load_overrides
from tools import genre_scanner, rom_importer
from tools.rom_safety import extract_zip_roms, load_gamelist_tree

ROOT = Path(__file__).resolve().parents[1]


class RomToolSafetyTests(unittest.TestCase):
    def test_override_configuration_is_data_only(self):
        overrides = load_overrides()
        self.assertIn("Super Mario Bros. 3", overrides)
        self.assertEqual("Platformer", overrides["Super Mario Bros. 3"])
        for relative in ("tools/installer.py", "tools/rom_importer.py"):
            source = (ROOT / relative).read_text(encoding="utf-8")
            self.assertNotIn("exec(", source, relative)
            self.assertNotIn("fix_unsorted.py", source, relative)

    def test_importer_variant_analysis_never_deletes(self):
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            names = ["Game (USA).gba", "Game [b1].gba", "Game [h1].gba"]
            for name in names:
                (folder / name).write_bytes(b"rom")
            logs = []
            flagged = rom_importer.clean_variants(folder, logs.append)
            self.assertGreater(flagged, 0)
            self.assertEqual(set(names), {p.name for p in folder.iterdir()})
            self.assertTrue(any("no action" in line for line in logs))

    def test_zip_extraction_only_writes_target_extensions(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            archive = root / "mixed.zip"
            dest = root / "GBA"
            dest.mkdir()
            with zipfile.ZipFile(archive, "w") as zf:
                zf.writestr("nested/Game.gba", b"gba")
                zf.writestr("payload.bin", b"not-for-gba")
            logs = []
            extracted = rom_importer.extract_zip(archive, dest, logs.append)
            self.assertEqual([dest / "Game.gba"], extracted)
            self.assertEqual(b"gba", (dest / "Game.gba").read_bytes())
            self.assertFalse((dest / "payload.bin").exists())

    def test_zip_basename_collision_aborts_before_writes(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            archive = root / "collision.zip"
            dest = root / "out"
            dest.mkdir()
            with zipfile.ZipFile(archive, "w") as zf:
                zf.writestr("disc1/Game.gba", b"one")
                zf.writestr("disc2/Game.gba", b"two")
            logs = []
            self.assertEqual([], extract_zip_roms(archive, dest, {".gba"}, logs.append))
            self.assertEqual([], list(dest.iterdir()))
            self.assertTrue(any("multiple ROM members" in line for line in logs))

    def test_importer_genre_scan_preserves_metadata(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            system = root / "GBA"
            system.mkdir()
            (system / "Existing.gba").write_bytes(b"old")
            (system / "New.gba").write_bytes(b"new")
            gamelist = system / "miyoogamelist.xml"
            gamelist.write_text(
                '<gameList><game><path>./Existing.gba</path><name>Existing</name>'
                '<genre>Action</genre><image>./Imgs/existing.png</image>'
                '<desc>preserve me</desc></game></gameList>', encoding="utf-8")
            db = root / "db.sqlite"
            db.touch()
            with patch("tools.rom_importer.db_lookup", return_value=("New", "Puzzle")):
                added = rom_importer.scan_genres_for_system(root, "GBA", db, lambda _m: None)
            self.assertEqual(1, added)
            result = gamelist.read_text(encoding="utf-8")
            self.assertIn("./Imgs/existing.png", result)
            self.assertIn("preserve me", result)
            self.assertIn("Puzzle", result)

    def test_importer_refuses_malformed_gamelist(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            system = root / "GBA"
            system.mkdir()
            (system / "Game.gba").write_bytes(b"rom")
            gamelist = system / "miyoogamelist.xml"
            original = b"<gameList><game>broken"
            gamelist.write_bytes(original)
            db = root / "db.sqlite"
            db.touch()
            logs = []
            self.assertEqual(0, rom_importer.scan_genres_for_system(root, "GBA", db, logs.append))
            self.assertEqual(original, gamelist.read_bytes())
            self.assertTrue(any("refusing to overwrite" in line for line in logs))

    def test_genre_override_preserves_unrelated_metadata(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "miyoogamelist.xml"
            path.write_text(
                '<gameList><game><path>./Super Mario Bros. 3.nes</path>'
                '<name>Super Mario Bros. 3</name><genre>Unsorted</genre>'
                '<image>./Imgs/mario.png</image><rating>0.95</rating></game></gameList>',
                encoding="utf-8")
            self.assertEqual(1, genre_scanner.apply_overrides(path))
            result = path.read_text(encoding="utf-8")
            self.assertIn("Platformer", result)
            self.assertIn("./Imgs/mario.png", result)
            self.assertIn("0.95", result)

    def test_shared_gamelist_loader_refuses_malformed_xml(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "bad.xml"
            path.write_text("<gameList><game>", encoding="utf-8")
            with self.assertRaises(Exception):
                load_gamelist_tree(path)
''', encoding="utf-8")

# Roadmap progress: archive/CD explicit selection remains future work, but unsafe
# auto-routing/extraction and executable override loading are now removed.
p = Path("docs/roadmap-v1.3-reliability.md")
text = p.read_text(encoding="utf-8")
text = text.replace(
    "- [ ] Remove executable Python override loading in favor of data-only overrides.\n",
    "- [x] Remove executable Python override loading in favor of data-only overrides.\n",
    1,
)
text = text.replace(
    "- [ ] Stream ZIP extraction with size/free-space/collision limits.\n",
    "- [x] Stream ZIP extraction with size/free-space/collision limits.\n",
    1,
)
if "- [x] Harden standalone importer/scanner metadata writes." not in text:
    text += "\n- [x] Harden standalone importer/scanner metadata writes.\n"
p.write_text(text, encoding="utf-8")
