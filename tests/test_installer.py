import tempfile
import unittest
from pathlib import Path

from tools.installer import audit_install, detect_onion, install_from_dir, uninstall
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


if __name__ == "__main__":
    unittest.main()
