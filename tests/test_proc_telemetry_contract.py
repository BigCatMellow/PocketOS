import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def production_proc_value(line: str, label: str) -> str:
    compiler = shutil.which("cc")
    if not compiler:
        raise unittest.SkipTest("a C compiler is required for the proc telemetry contract")
    source = (ROOT / "src" / "pocketOS" / "pocketOS.c").read_text(encoding="utf-8")
    start = source.index("static long proc_kb_line_value")
    end = source.index("static long proc_kb_value", start)
    parser = source[start:end]
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        harness = root / "proc.c"
        binary = root / "proc"
        harness.write_text(
            "#include <ctype.h>\n#include <errno.h>\n#include <stdio.h>\n"
            "#include <stdlib.h>\n#include <string.h>\n" + parser + "\n"
            "int main(int argc, char **argv) {\n"
            "  if (argc != 3) return 2;\n"
            "  printf(\"%ld\\n\", proc_kb_line_value(argv[1], argv[2]));\n"
            "  return 0;\n"
            "}\n",
            encoding="utf-8",
        )
        subprocess.run([compiler, "-Wall", "-Werror", "-o", str(binary), str(harness)], check=True)
        return subprocess.run([str(binary), line, label], text=True, capture_output=True, check=True).stdout.strip()


class ProcTelemetryContractTests(unittest.TestCase):
    def test_realistic_proc_status_and_meminfo_lines(self):
        fixture = (
            "Name:\tpocketOS\n"
            "VmPeak:\t32832 kB\n"
            "VmSize:\t32832 kB\n"
            "VmRSS:\t15472 kB\n"
            "MemTotal:\t128000 kB\n"
            "MemFree:\t19000 kB\n"
            "MemAvailable:\t51000 kB\n"
        )
        values = {
            line.split(":", 1)[0] + ":": production_proc_value(line, line.split(":", 1)[0] + ":")
            for line in fixture.splitlines()
        }
        self.assertEqual("15472", values["VmRSS:"])
        self.assertEqual("51000", values["MemAvailable:"])
        self.assertEqual("15472", production_proc_value("VmRSS:\t15472 kB\n", "VmRSS:"))
        self.assertEqual("-1", production_proc_value("Name:\tpocketOS", "VmRSS:"))

    def test_malformed_numeric_telemetry_is_unavailable_not_zero(self):
        self.assertEqual("-1", production_proc_value("VmRSS:\t123junk kB", "VmRSS:"))
        self.assertEqual("-1", production_proc_value("MemAvailable:\t kB", "MemAvailable:"))
