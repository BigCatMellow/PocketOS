import tempfile
import unittest
import zipfile
import zlib
from pathlib import Path
from unittest.mock import patch

from tools.installer import (
    POCKETOS_TRANSACTION_PATHS, _crc32_of, _select_candidate, audit_install,
    clean_variants, detect_onion, extract_zip, install_from_dir, scan_genres_for_system, uninstall,
)
from tools.install_runtime import install_payload
from tools.onion_runtime import BEGIN_MARKER

from tests.test_onion_runtime import STOCK_RUNTIME


class InstallerTests(unittest.TestCase):
    @staticmethod
    def _owned_state(sd: Path):
        state = {}
        for relative in POCKETOS_TRANSACTION_PATHS:
            target = sd / relative
            if target.is_symlink():
                state[str(relative)] = ("symlink", target.readlink().as_posix())
            elif target.is_file():
                state[str(relative)] = ("file", target.read_bytes(), target.stat().st_mode & 0o777)
            elif target.is_dir():
                files = {}
                for child in sorted(target.rglob("*")):
                    rel = child.relative_to(target).as_posix()
                    if child.is_symlink():
                        files[rel] = ("symlink", child.readlink().as_posix())
                    elif child.is_file():
                        files[rel] = ("file", child.read_bytes(), child.stat().st_mode & 0o777)
                    elif child.is_dir():
                        files[rel] = ("dir",)
                state[str(relative)] = ("dir", files)
            else:
                state[str(relative)] = None
        return state

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

    def test_sd_candidate_selection_reprompts_until_valid(self):
        choices = iter(["nonsense", "0", "3", "2"])
        warnings = []
        candidates = [Path("/card-one"), Path("/card-two")]
        selected = _select_candidate(candidates, lambda _prompt: next(choices), warnings.append)
        self.assertEqual(Path("/card-two"), selected)
        self.assertEqual(3, len(warnings))
        self.assertTrue(all("1 to 2" in warning for warning in warnings))

    def test_failed_update_rolls_back_all_owned_paths(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            payload, sd, runtime = self._make_payload_and_sd(root)
            install_from_dir(payload, sd, lambda _message: None)

            binary = sd / ".tmp_update" / "bin" / "pocketOS"
            binary.write_bytes(b"old installed binary")
            assets = sd / ".tmp_update" / "res" / "pocketos"
            (assets / "old-only.txt").write_text("keep me")
            helper = sd / "pocketos-health-report.py"
            helper.write_text("old report")
            before = self._owned_state(sd)

            (payload / ".tmp_update" / "bin" / "pocketOS").write_bytes(b"new binary")
            (payload / ".tmp_update" / "res" / "pocketos" / "new.txt").write_text("new")

            from tools import installer as installer_module
            real_hook = installer_module.install_runtime_hook

            def fail_after_runtime_change(card):
                real_hook(card)
                raise RuntimeError("injected failure after runtime patch")

            with patch("tools.installer.install_runtime_hook", side_effect=fail_after_runtime_change):
                with self.assertRaisesRegex(RuntimeError, "injected failure"):
                    install_from_dir(payload, sd, lambda _message: None)

            self.assertEqual(before, self._owned_state(sd))
            self.assertFalse(any((sd / ".tmp_update").glob(".pocketos-transaction-*")))

    def test_failed_fresh_install_leaves_no_pocketos_artifacts(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            payload, sd, _runtime = self._make_payload_and_sd(root)
            before = self._owned_state(sd)
            with patch("tools.installer.install_runtime_hook", side_effect=RuntimeError("hook failed")):
                with self.assertRaisesRegex(RuntimeError, "hook failed"):
                    install_from_dir(payload, sd, lambda _message: None)
            self.assertEqual(before, self._owned_state(sd))
            self.assertFalse(any((sd / ".tmp_update").glob(".pocketos-transaction-*")))

    def test_failed_uninstall_restores_runtime_and_files(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            payload, sd, _runtime = self._make_payload_and_sd(root)
            install_from_dir(payload, sd, lambda _message: None)
            (sd / "pocketos-health-report.py").write_text("report")
            before = self._owned_state(sd)

            from tools import installer as installer_module
            real_remove = installer_module._remove_owned_file
            calls = {"count": 0}

            def fail_after_first_remove(path, log, label):
                real_remove(path, log, label)
                calls["count"] += 1
                if calls["count"] == 1:
                    raise RuntimeError("injected uninstall failure")

            with patch("tools.installer._remove_owned_file", side_effect=fail_after_first_remove):
                with self.assertRaisesRegex(RuntimeError, "injected uninstall failure"):
                    uninstall(sd, lambda _message: None)

            self.assertEqual(before, self._owned_state(sd))
            self.assertFalse(any((sd / ".tmp_update").glob(".pocketos-transaction-*")))

    def test_successful_uninstall_removes_root_helpers(self):
        with tempfile.TemporaryDirectory() as temp:
            payload, sd, _runtime = self._make_payload_and_sd(Path(temp))
            install_from_dir(payload, sd, lambda _message: None)
            for name in (
                "pocketos-health-report.py", "pocketos-stress-test.sh",
                "onion-baseline-monitor.sh", "launcher-comparison-monitor.sh",
            ):
                (sd / name).write_text("helper")
            uninstall(sd, lambda _message: None)
            for name in (
                "pocketos-health-report.py", "pocketos-stress-test.sh",
                "onion-baseline-monitor.sh", "launcher-comparison-monitor.sh",
            ):
                self.assertFalse((sd / name).exists(), name)

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


if __name__ == "__main__":
    unittest.main()
