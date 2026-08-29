import tempfile
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
