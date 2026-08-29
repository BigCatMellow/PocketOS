import tempfile
import unittest
import zlib
from pathlib import Path
from unittest.mock import patch

from tools.installer import (
    _crc32_of, audit_install, clean_variants, detect_onion,
    install_from_dir, scan_genres_for_system, uninstall,
)
from tools.install_runtime import install_payload
from tools.onion_runtime import BEGIN_MARKER

from tests.test_onion_runtime import STOCK_RUNTIME


class InstallerTests(unittest.TestCase):
    @staticmethod
    def _make_payload_and_sd(root: Path):
        payload = root / "payload"
        sd = root / "sd"
        (payload / ".tmp_update" / "bin").mkdir(parents=True)
        (payload / ".tmp_update" / "res" / "pocketos").mkdir(parents=True)
        binary = payload / ".tmp_update" / "bin" / "pocketOS"
        binary.write_bytes(b"arm binary")
        binary.chmod(0o644)
        (payload / ".tmp_update" / "res" / "pocketos" / "theme.json").write_text("{}")
        (sd / ".tmp_update").mkdir(parents=True)
        (sd / ".tmp_update" / "onionVersion").mkdir()
        (sd / ".tmp_update" / "onionVersion" / "version.txt").write_text("v4.3.1-1")
        (sd / "Roms").mkdir()
        runtime = sd / ".tmp_update" / "runtime.sh"
        runtime.write_text(STOCK_RUNTIME)
        runtime.chmod(0o755)
        return payload, sd, runtime

    def test_install_and_uninstall_manage_complete_contract(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            payload, sd, runtime = self._make_payload_and_sd(root)

            install_from_dir(payload, sd, lambda _message: None)
            self.assertEqual(b"arm binary", (sd / ".tmp_update" / "bin" / "pocketOS").read_bytes())
            self.assertEqual(0o755, (sd / ".tmp_update" / "bin" / "pocketOS").stat().st_mode & 0o777)
            self.assertTrue((sd / ".tmp_update" / "res" / "pocketos" / "theme.json").is_file())
            self.assertIn(BEGIN_MARKER, runtime.read_text())

            uninstall(sd, lambda _message: None)
            self.assertFalse((sd / ".tmp_update" / "bin" / "pocketOS").exists())
            self.assertFalse((sd / ".tmp_update" / "res" / "pocketos").exists())
            self.assertNotIn(BEGIN_MARKER, runtime.read_text())

    def test_manual_release_installer_copies_payload_to_separate_sd_root(self):
        with tempfile.TemporaryDirectory() as temp:
            payload, sd, _runtime = self._make_payload_and_sd(Path(temp))

            install_payload(payload, sd)

            binary = sd / ".tmp_update" / "bin" / "pocketOS"
            self.assertEqual(b"arm binary", binary.read_bytes())
            self.assertEqual(0o755, binary.stat().st_mode & 0o777)
            self.assertEqual(
                "{}",
                (sd / ".tmp_update" / "res" / "pocketos" / "theme.json").read_text(),
            )

    def test_updates_preserve_the_active_theme(self):
        with tempfile.TemporaryDirectory() as temp:
            payload, sd, _runtime = self._make_payload_and_sd(Path(temp))
            active_theme = sd / ".tmp_update" / "res" / "pocketos" / "theme.json"
            active_theme.parent.mkdir(parents=True)
            active_theme.write_text('{"active": "cloud"}')

            install_payload(payload, sd)
            self.assertEqual('{"active": "cloud"}', active_theme.read_text())

            active_theme.write_text('{"active": "sage"}')
            install_from_dir(payload, sd, lambda _message: None)
            self.assertEqual('{"active": "sage"}', active_theme.read_text())

    def test_audit_detects_current_onion_version_marker(self):
        with tempfile.TemporaryDirectory() as temp:
            payload, sd, _runtime = self._make_payload_and_sd(Path(temp))
            self.assertTrue(detect_onion(sd))
            errors, warnings = audit_install(sd, payload)
        self.assertEqual([], errors)
        self.assertEqual([], warnings)

    def test_audit_blocks_missing_runtime_before_copying(self):
        with tempfile.TemporaryDirectory() as temp:
            payload, sd, runtime = self._make_payload_and_sd(Path(temp))
            runtime.unlink()
            errors, _warnings = audit_install(sd, payload)
            self.assertTrue(any("runtime is missing" in error for error in errors))
            with self.assertRaises(RuntimeError):
                install_from_dir(payload, sd, lambda _message: None)

    def test_audit_blocks_incomplete_runtime_markers(self):
        with tempfile.TemporaryDirectory() as temp:
            payload, sd, runtime = self._make_payload_and_sd(Path(temp))
            runtime.write_text(runtime.read_text() + BEGIN_MARKER)
            errors, _warnings = audit_install(sd, payload)
        self.assertTrue(any("markers are incomplete" in error for error in errors))

    def test_variant_analysis_never_deletes_multidisc_or_cue_bin_files(self):
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            names = ["Final Fantasy VII (Disc 1).chd", "Final Fantasy VII (Disc 2).chd",
                     "Final Fantasy VII (Disc 3).chd", "Metal Gear Solid.bin", "Metal Gear Solid.cue"]
            for name in names:
                (folder / name).write_bytes(b"rom")
            logs = []
            flagged = clean_variants(folder, logs.append)
            self.assertGreater(flagged, 0)
            self.assertEqual(set(names), {item.name for item in folder.iterdir()})
            self.assertTrue(any("no action" in line for line in logs))

    def test_crc32_reads_past_64_mib(self):
        with tempfile.TemporaryDirectory() as temp:
            rom = Path(temp) / "large.bin"
            block = b"PocketOS" * 1024
            crc = 0
            with rom.open("wb") as handle:
                remaining = 64 * 1024 * 1024 + 17
                while remaining:
                    chunk = block[:min(len(block), remaining)]
                    handle.write(chunk)
                    crc = zlib.crc32(chunk, crc)
                    remaining -= len(chunk)
            self.assertEqual(f"{crc & 0xFFFFFFFF:08X}", _crc32_of(rom))

    def test_genre_scan_preserves_existing_metadata(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            system = root / "GBA"
            system.mkdir()
            (system / "Existing.gba").write_bytes(b"old")
            (system / "New.gba").write_bytes(b"new")
            gamelist = system / "miyoogamelist.xml"
            gamelist.write_text('<gameList><game><path>./Existing.gba</path><name>Existing</name><genre>Action</genre><image>./Imgs/existing.png</image><desc>Keep this description</desc><rating>0.9</rating></game></gameList>', encoding="utf-8")
            db = root / "dummy.sqlite"
            db.touch()
            with patch("tools.installer._db_lookup", return_value=("New Game", "Puzzle")):
                added = scan_genres_for_system(root, "GBA", db, lambda _m: None)
            self.assertEqual(1, added)
            result = gamelist.read_text(encoding="utf-8")
            self.assertIn("./Imgs/existing.png", result)
            self.assertIn("Keep this description", result)
            self.assertIn("<rating>0.9</rating>", result)
            self.assertIn("New Game", result)

    def test_genre_scan_refuses_to_overwrite_malformed_xml(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            system = root / "GBA"
            system.mkdir()
            (system / "Game.gba").write_bytes(b"rom")
            gamelist = system / "miyoogamelist.xml"
            original = b"<gameList><game>broken"
            gamelist.write_bytes(original)
            db = root / "dummy.sqlite"
            db.touch()
            logs = []
            self.assertEqual(0, scan_genres_for_system(root, "GBA", db, logs.append))
            self.assertEqual(original, gamelist.read_bytes())
            self.assertTrue(any("refusing to overwrite" in line for line in logs))


if __name__ == "__main__":
    unittest.main()
