from pathlib import Path
import re


def replace_once(path, old, new):
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    if old not in text:
        raise SystemExit(f"expected text not found in {path}: {old[:100]!r}")
    p.write_text(text.replace(old, new, 1), encoding="utf-8")


def sub_once(path, pattern, replacement):
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    text, count = re.subn(pattern, replacement, text, count=1, flags=re.S)
    if count != 1:
        raise SystemExit(f"pattern failed in {path}: {pattern[:100]!r}")
    p.write_text(text, encoding="utf-8")


registry_import = '''try:
    from .onion_systems import ROM_EXTENSIONS, candidates_for_extension, openvgdb_system_name
except ImportError:  # Direct script and PyInstaller execution.
    from onion_systems import ROM_EXTENSIONS, candidates_for_extension, openvgdb_system_name
'''

# Installer: remove its private system truth and use the canonical registry.
p = Path("tools/installer.py")
text = p.read_text(encoding="utf-8")
anchor = '''except ImportError:  # Direct script and PyInstaller execution.
    from onion_runtime import BEGIN_MARKER, END_MARKER, install_runtime_hook, remove_runtime_hook
'''
if anchor not in text:
    raise SystemExit("installer import anchor missing")
text = text.replace(anchor, anchor + "\n" + registry_import, 1)
text, count = re.subn(
    r'''# ── ROM import constants ─+\n\nEXT_TO_SYSTEMS = \{.*?\nSYSTEM_MAP = \{.*?\n\}\n''',
    '''# ── ROM import constants ──────────────────────────────────────────────────────\n\nROM_EXTS = set(ROM_EXTENSIONS)\n\nDOC_NAMES = {"readme", "license", "changelog", "credits", "notes", "info", "manual"}\n''',
    text,
    count=1,
    flags=re.S,
)
if count != 1:
    raise SystemExit("installer map removal failed")
p.write_text(text, encoding="utf-8")

sub_once(
    "tools/installer.py",
    r'''def detect_system\(zip_path: Path\):\n.*?\n    return None, \[\]\n''',
    '''def detect_system(zip_path: Path):
    """Return a safe extension-derived Onion target; ambiguous formats are skipped."""
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
''')

replace_once(
    "tools/installer.py",
    '''    system_name = SYSTEM_MAP.get(system_folder.upper())
    if not system_name:
        return 0
''',
    '''    system_name = openvgdb_system_name(system_folder)
    if not system_name:
        return 0
''')
replace_once(
    "tools/installer.py",
    '''        result = _db_lookup(db, rom, system_name)
''',
    '''        rom_system_name = openvgdb_system_name(system_folder, rom.suffix) or system_name
        result = _db_lookup(db, rom, rom_system_name)
''')
replace_once(
    "tools/installer.py",
    '''                if dest_folder is None:
                    dest_folder = roms_root / candidates[0]
                    dest_folder.mkdir(parents=True, exist_ok=True)
                _info(f"[{dest_folder.name}] {zip_path.name}")
''',
    '''                if dest_folder is None:
                    _warn(f"[{candidates[0]}] {zip_path.name} — Onion system folder is not installed; skipping")
                    skipped += 1
                    continue
                _info(f"[{dest_folder.name}] {zip_path.name}")
''')

# Standalone importer: same registry and same no-invented-folder rule.
p = Path("tools/rom_importer.py")
text = p.read_text(encoding="utf-8")
import_anchor = 'from tkinter import ttk, filedialog, scrolledtext, messagebox\n'
if import_anchor not in text:
    raise SystemExit("rom_importer import anchor missing")
text = text.replace(import_anchor, import_anchor + "\n" + registry_import, 1)
text, count = re.subn(
    r'''# ── Extension → candidate system folder names .*?# ── Genre scanning helpers''',
    '''# ── Onion ROM/system contract ─────────────────────────────────────────────────\nROM_EXTS = set(ROM_EXTENSIONS)\nDOC_NAMES = {"readme", "license", "changelog", "credits", "notes", "info", "manual"}\n\n# ── Genre scanning helpers''',
    text,
    count=1,
    flags=re.S,
)
if count != 1:
    raise SystemExit("rom_importer map removal failed")
p.write_text(text, encoding="utf-8")

sub_once(
    "tools/rom_importer.py",
    r'''def detect_system\(zip_path: Path\) -> tuple\[str \| None, list\[str\]\]:\n.*?\n    return None, \[\]\n''',
    '''def detect_system(zip_path: Path) -> tuple[str | None, list[str]]:
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
''')
replace_once(
    "tools/rom_importer.py",
    '''    system_name = SYSTEM_MAP.get(system_folder.upper())
    if not system_name:
        log(f"  genre scan: no DB mapping for {system_folder}, skipping")
        return 0
''',
    '''    system_name = openvgdb_system_name(system_folder)
    if not system_name:
        log(f"  genre scan: no DB mapping for {system_folder}, skipping")
        return 0
''')
replace_once(
    "tools/rom_importer.py",
    '''        result = db_lookup(db, rom, system_name)
''',
    '''        rom_system_name = openvgdb_system_name(system_folder, rom.suffix) or system_name
        result = db_lookup(db, rom, rom_system_name)
''')
replace_once(
    "tools/rom_importer.py",
    '''            if dest_folder is None:
                # Create the first candidate folder
                dest_folder = roms_root / candidates[0]
                dest_folder.mkdir(parents=True, exist_ok=True)
                self.log(f"[+] Created folder: {dest_folder.name}")

            self.log(f"[{dest_folder.name}] {zip_path.name}")
''',
    '''            if dest_folder is None:
                self.log(f"[{candidates[0]}] {zip_path.name} — Onion system folder is not installed; skipping")
                skipped += 1
                continue

            self.log(f"[{dest_folder.name}] {zip_path.name}")
''')

# Genre scanner: derive scan identity from the same registry.
p = Path("tools/genre_scanner.py")
text = p.read_text(encoding="utf-8")
import_anchor = 'import urllib.request\n'
if import_anchor not in text:
    raise SystemExit("genre scanner import anchor missing")
text = text.replace(import_anchor, import_anchor + "\n" + registry_import, 1)
text, count = re.subn(
    r'''# ── System folder → OpenVGDB system name ─+\nSYSTEM_MAP = \{.*?\nROM_EXTS = \{.*?\n\}\n''',
    '''# ── Onion ROM/system contract ─────────────────────────────────────────────────\nROM_EXTS = set(ROM_EXTENSIONS)\n''',
    text,
    count=1,
    flags=re.S,
)
if count != 1:
    raise SystemExit("genre scanner map removal failed")
p.write_text(text, encoding="utf-8")
replace_once(
    "tools/genre_scanner.py",
    '''            system_name = SYSTEM_MAP.get(folder.name.upper())
            if not system_name:
                continue
''',
    '''            system_name = openvgdb_system_name(folder.name)
            if not system_name:
                continue
''')
replace_once(
    "tools/genre_scanner.py",
    '''                result = db_lookup(conn, rom, system_name)
''',
    '''                rom_system_name = openvgdb_system_name(folder.name, rom.suffix) or system_name
                result = db_lookup(conn, rom, rom_system_name)
''')

# The launcher reads actual Onion Emu configs; only repair its human-facing
# legacy aliases so NEOGEO is never mislabeled as a Pocket system.
p = Path("src/pocketOS/pocketOS.c")
text = p.read_text(encoding="utf-8")
text = text.replace('''    if (strcasecmp(label, "VBOY")    == 0) return "Virtual Boy";
''', '''    if (strcasecmp(label, "VB")      == 0 || strcasecmp(label, "VBOY") == 0) return "Virtual Boy";
''', 1)
text = text.replace('''    if (strcasecmp(label, "SMS")     == 0) return "Master System";
''', '''    if (strcasecmp(label, "MS")      == 0 || strcasecmp(label, "SMS") == 0) return "Master System";
''', 1)
text = text.replace('''    if (strcasecmp(label, "SCD")     == 0) return "Sega CD";
    if (strcasecmp(label, "32X")     == 0) return "Sega 32X";
''', '''    if (strcasecmp(label, "SEGACD")  == 0 || strcasecmp(label, "SCD") == 0) return "Sega CD";
    if (strcasecmp(label, "THIRTYTWOX") == 0 || strcasecmp(label, "32X") == 0) return "Sega 32X";
''', 1)
text = text.replace('''    if (strcasecmp(label, "NEOGEO")  == 0 ||
        strcasecmp(label, "NGP")     == 0) return "Neo Geo Pocket";
    if (strcasecmp(label, "NGPC")    == 0) return "Neo Geo Pocket Color";
''', '''    if (strcasecmp(label, "NEOGEO")  == 0) return "Neo Geo";
    if (strcasecmp(label, "NEOCD")   == 0) return "Neo Geo CD";
    if (strcasecmp(label, "NGP")     == 0 || strcasecmp(label, "NGPC") == 0)
        return "Neo Geo Pocket / Color";
''', 1)
text = text.replace('''    if (strcasecmp(label, "2600")    == 0 ||
        strcasecmp(label, "ATARI2600") == 0) return "Atari 2600";
    if (strcasecmp(label, "WSWAN")   == 0) return "WonderSwan";
    if (strcasecmp(label, "WSWANC")  == 0) return "WonderSwan Color";
''', '''    if (strcasecmp(label, "ATARI")   == 0 || strcasecmp(label, "2600") == 0 ||
        strcasecmp(label, "ATARI2600") == 0) return "Atari 2600";
    if (strcasecmp(label, "WS")      == 0 || strcasecmp(label, "WSWAN") == 0 ||
        strcasecmp(label, "WSWANC")  == 0) return "WonderSwan / Color";
''', 1)
p.write_text(text, encoding="utf-8")

Path("tests/test_onion_systems.py").write_text(r'''import unittest
from pathlib import Path

from tools.onion_systems import (
    AMBIGUOUS_EXTENSIONS,
    candidates_for_extension,
    canonical_folder,
    display_name,
    openvgdb_system_name,
)

ROOT = Path(__file__).resolve().parents[1]


class OnionSystemRegistryTests(unittest.TestCase):
    def test_canonical_folder_names_match_onion_contract(self):
        self.assertEqual(("MS",), candidates_for_extension(".sms"))
        self.assertEqual(("WS",), candidates_for_extension(".ws"))
        self.assertEqual(("WS",), candidates_for_extension(".wsc"))
        self.assertEqual(("FDS",), candidates_for_extension(".fds"))
        self.assertEqual(("NGP",), candidates_for_extension(".ngc"))
        self.assertEqual("VB", canonical_folder("VBOY"))
        self.assertEqual("SEGACD", canonical_folder("SCD"))
        self.assertEqual("THIRTYTWOX", canonical_folder("32X"))

    def test_neogeo_and_pocket_are_distinct_systems(self):
        self.assertEqual("Neo Geo", display_name("NEOGEO"))
        self.assertEqual("Neo Geo Pocket / Color", display_name("NGP"))
        self.assertEqual("SNK Neo Geo Pocket", openvgdb_system_name("NGP", ".ngp"))
        self.assertEqual("SNK Neo Geo Pocket Color", openvgdb_system_name("NGP", ".ngc"))

    def test_disc_and_archive_extensions_are_never_auto_routed(self):
        for ext in (".bin", ".cue", ".iso", ".img", ".chd", ".zip", ".7z"):
            with self.subTest(ext=ext):
                self.assertIn(ext, AMBIGUOUS_EXTENSIONS)
                self.assertEqual((), candidates_for_extension(ext))

    def test_optional_n64_is_existing_folder_only(self):
        self.assertEqual(("N64",), candidates_for_extension(".z64"))

    def test_desktop_tools_do_not_define_competing_system_maps(self):
        for relative in ("tools/installer.py", "tools/rom_importer.py", "tools/genre_scanner.py"):
            source = (ROOT / relative).read_text(encoding="utf-8")
            self.assertNotIn("EXT_TO_SYSTEMS =", source, relative)
            self.assertNotIn("SYSTEM_MAP =", source, relative)
            self.assertIn("onion_systems", source, relative)

    def test_launcher_does_not_label_neogeo_as_pocket(self):
        source = (ROOT / "src/pocketOS/pocketOS.c").read_text(encoding="utf-8")
        start = source.index("static const char *system_full_name")
        end = source.index("static const char *library_system_name", start)
        mapping = source[start:end]
        self.assertIn('strcasecmp(label, "NEOGEO")  == 0) return "Neo Geo"', mapping)
        self.assertNotIn('strcasecmp(label, "NEOGEO")  == 0 ||', mapping)
''', encoding="utf-8")

replace_once(
    "docs/roadmap-v1.3-reliability.md",
    "- [ ] Consolidate canonical Onion system/folder mappings.\n",
    "- [x] Consolidate canonical Onion system/folder mappings.\n",
)
