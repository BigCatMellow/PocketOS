import contextlib
import csv
import io
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
from tools import pocketos_health_report


class HealthReportTests(unittest.TestCase):
    @staticmethod
    def _write_log(root: Path, rss: str, available: str):
        path = root / ".tmp_update" / "logs" / "pocketos_health.csv"
        path.parent.mkdir(parents=True)
        with path.open("w", newline="", encoding="utf-8") as handle:
            writer = csv.DictWriter(handle, fieldnames=["timestamp", "event", "screen", "rss_kb", "mem_available_kb", "battery_percent", "brightness"])
            writer.writeheader()
            writer.writerow({"timestamp": "1700000000", "event": "minute", "screen": "HOME", "rss_kb": rss, "mem_available_kb": available, "battery_percent": "80", "brightness": "5"})

    def test_invalid_memory_telemetry_fails_report(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self._write_log(root, "-1", "-1")
            output = io.StringIO()
            with patch.object(sys, "argv", ["report", str(root)]), contextlib.redirect_stdout(output):
                code = pocketos_health_report.main()
            self.assertEqual(2, code)
            self.assertIn("INVALID TELEMETRY", output.getvalue())

    def test_valid_memory_telemetry_passes_report(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self._write_log(root, "12345", "54321")
            with patch.object(sys, "argv", ["report", str(root)]), contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(0, pocketos_health_report.main())
