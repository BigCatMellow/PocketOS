import os
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools.onion_runtime import (
    BACKUP_REL,
    BEGIN_MARKER,
    END_MARKER,
    RuntimePatchError,
    install_runtime_hook,
    patch_runtime_text,
    remove_runtime_hook,
    unpatch_runtime_text,
)


STOCK_RUNTIME = """#!/bin/sh
check_main_ui() {
    # MainUI launch
    cd $miyoodir/app
    PATH="$miyoodir/app:$PATH" \\
        LD_LIBRARY_PATH="$miyoodir/lib:/config/lib:/lib" \\
        LD_PRELOAD="$miyoodir/lib/libpadsp.so" \\
        ./MainUI 2>&1 > /dev/null

    # Merge the last game launched into the recent list
    printf reached > "$TEST_ROOT/reached"
}
check_main_ui
"""


class RuntimeTextTests(unittest.TestCase):
    def test_patch_is_idempotent_and_valid_shell(self):
        patched = patch_runtime_text(STOCK_RUNTIME)
        self.assertEqual(patched, patch_runtime_text(patched))
        self.assertEqual(1, patched.count(BEGIN_MARKER))
        self.assertEqual(1, patched.count(END_MARKER))
        with tempfile.NamedTemporaryFile("w", suffix=".sh") as script:
            script.write(patched)
            script.flush()
            subprocess.run(["bash", "-n", script.name], check=True)

    def test_unpatch_removes_only_managed_hook(self):
        restored = unpatch_runtime_text(patch_runtime_text(STOCK_RUNTIME))
        self.assertNotIn(BEGIN_MARKER, restored)
        self.assertIn("./MainUI > /dev/null 2>&1", restored)
        self.assertIn("printf reached", restored)

    def test_incomplete_markers_are_rejected(self):
        with self.assertRaises(RuntimePatchError):
            patch_runtime_text(STOCK_RUNTIME + BEGIN_MARKER)


class RuntimeBehaviorTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        (self.root / ".tmp_update" / "bin").mkdir(parents=True)
        (self.root / "miyoo" / "app").mkdir(parents=True)
        self.runtime = self.root / ".tmp_update" / "runtime.sh"
        self.runtime.write_text(STOCK_RUNTIME, encoding="utf-8")
        self.runtime.chmod(0o755)
        self.mainui_log = self.root / "mainui.log"
        self._write_executable(
            self.root / "miyoo" / "app" / "MainUI",
            '#!/bin/sh\nprintf "mainui\\n" >> "$TEST_ROOT/mainui.log"\n',
        )
        self.fail_flag = self.root / "pocketos_failed"

    def tearDown(self):
        self.temp.cleanup()

    @staticmethod
    def _write_executable(path: Path, text: str):
        path.write_text(text, encoding="utf-8")
        path.chmod(0o755)

    def _run(self):
        env = os.environ.copy()
        env.update({
            "TEST_ROOT": str(self.root),
            "POCKETOS_FAIL_FLAG": str(self.fail_flag),
            "sysdir": str(self.root / ".tmp_update"),
            "miyoodir": str(self.root / "miyoo"),
        })
        subprocess.run(
            ["bash", str(self.runtime)], env=env, check=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        )

    def test_failed_pocketos_falls_back_and_stays_on_mainui(self):
        pocketos = self.root / ".tmp_update" / "bin" / "pocketOS"
        self._write_executable(pocketos, "#!/bin/sh\nexit 42\n")
        install_runtime_hook(self.root)

        self._run()
        self.assertTrue(self.fail_flag.exists())
        self.assertEqual("mainui\n", self.mainui_log.read_text())
        self.assertIn("status 42", (self.root / ".tmp_update" / "logs" / "pocketos_runtime.log").read_text())

        self._write_executable(pocketos, "#!/bin/sh\nexit 0\n")
        self._run()
        self.assertEqual("mainui\nmainui\n", self.mainui_log.read_text())

    def test_successful_pocketos_does_not_launch_mainui(self):
        self._write_executable(
            self.root / ".tmp_update" / "bin" / "pocketOS",
            '#!/bin/sh\nprintf "pocketos\\n" > "$TEST_ROOT/pocketos.log"\n',
        )
        install_runtime_hook(self.root)
        self._run()
        self.assertFalse(self.mainui_log.exists())
        self.assertFalse(self.fail_flag.exists())
        self.assertEqual("pocketos\n", (self.root / "pocketos.log").read_text())

    def test_file_install_preserves_backup_and_mode(self):
        install_runtime_hook(self.root)
        self.assertTrue((self.root / BACKUP_REL).is_file())
        self.assertEqual(0o755, self.runtime.stat().st_mode & 0o777)
        self.assertTrue(remove_runtime_hook(self.root))
        self.assertEqual(STOCK_RUNTIME, self.runtime.read_text())

    def test_uninstall_preserves_unrelated_runtime_changes(self):
        install_runtime_hook(self.root)
        self.runtime.write_text(self.runtime.read_text() + "# user customization\n")

        self.assertTrue(remove_runtime_hook(self.root))
        restored = self.runtime.read_text()
        self.assertTrue(restored.endswith("# user customization\n"))
        self.assertIn("./MainUI 2>&1 > /dev/null", restored)

    def test_reinstall_after_stock_runtime_change_refreshes_backup(self):
        first = STOCK_RUNTIME.replace("printf reached", "printf first")
        second = STOCK_RUNTIME.replace("printf reached", "printf second")
        self.runtime.write_text(first, encoding="utf-8")
        install_runtime_hook(self.root)
        backup = self.root / BACKUP_REL
        self.assertEqual(first, backup.read_text(encoding="utf-8"))

        # Simulate Onion replacing runtime.sh with a newer stock runtime.
        self.runtime.write_text(second, encoding="utf-8")
        self.runtime.chmod(0o755)
        install_runtime_hook(self.root)
        self.assertEqual(second, backup.read_text(encoding="utf-8"))

        self.assertTrue(remove_runtime_hook(self.root))
        restored = self.runtime.read_text(encoding="utf-8")
        self.assertIn("printf second", restored)
        self.assertNotIn("printf first", restored)


if __name__ == "__main__":
    unittest.main()
