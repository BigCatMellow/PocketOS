import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def production_serial_validator(serial: str) -> int:
    compiler = shutil.which("cc")
    if not compiler:
        raise unittest.SkipTest("a C compiler is required for the persistence contract")
    source = (ROOT / "src" / "pocketOS" / "pocketOS.c").read_text(encoding="utf-8")
    start = source.index("static int device_serial_is_safe")
    end = source.index("static int json_write_string", start)
    validator = source[start:end]
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        harness = root / "serial.c"
        binary = root / "serial"
        harness.write_text(
            "#include <ctype.h>\n#include <stddef.h>\n" + validator + "\n"
            "int main(int argc, char **argv) {\n"
            "  return argc == 2 && device_serial_is_safe(argv[1]) ? 0 : 1;\n"
            "}\n",
            encoding="utf-8",
        )
        subprocess.run([compiler, "-Wall", "-Werror", "-o", str(binary), str(harness)], check=True)
        return subprocess.run([str(binary), serial], check=False).returncode


class PersistenceContractTests(unittest.TestCase):
    def test_device_serial_is_allowlisted_by_production_c_helper(self):
        self.assertEqual(0, production_serial_validator("MMP-serial_42"))
        for unsafe in ("../escape", "x;touch", "$(id)", "two words", "quote'", "x\nnext"):
            with self.subTest(unsafe=unsafe):
                self.assertNotEqual(0, production_serial_validator(unsafe))

    def test_serial_persistence_does_not_build_a_shell_command(self):
        source = (ROOT / "src" / "pocketOS" / "pocketOS.c").read_text(encoding="utf-8")
        block = source[source.index("// Persist settings so they survive reboot"):]
        self.assertIn("device_serial_is_safe(sn)", block)
        self.assertIn("copy_file_atomic", block)
        self.assertNotIn("system(cmd)", block)

    def test_library_truncation_is_logged_and_visible(self):
        source = (ROOT / "src" / "pocketOS" / "pocketOS.c").read_text(encoding="utf-8")
        for flag in (
            "systems_truncated", "games_truncated", "browse_games_truncated", "browse_genres_truncated",
        ):
            self.assertIn(flag, source)
        self.assertIn("LIBRARY LIMIT REACHED — SOME ENTRIES HIDDEN", source)
        self.assertIn("library truncated: system limit reached", source)
        self.assertIn("library truncated: game limit reached", source)
