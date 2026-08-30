import unittest
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

    def test_explicit_targets_can_accept_ambiguous_formats_without_auto_routing(self):
        from tools.onion_systems import extensions_for_folder
        self.assertTrue({".cue", ".bin", ".chd"}.issubset(extensions_for_folder("PS")))
        self.assertEqual(frozenset({".zip"}), extensions_for_folder("NEOGEO"))

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
