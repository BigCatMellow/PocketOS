import json
import tempfile
import unittest
from pathlib import Path

from tools.check_environment import check_environment, load_spec


ROOT = Path(__file__).resolve().parents[1]


class EnvironmentContractTests(unittest.TestCase):
    def test_environment_spec_profiles_are_present(self):
        spec = load_spec(ROOT / "pocketos" / "environment.json")
        self.assertEqual("pocketos-release", spec["environment_id"])
        self.assertEqual("1.2.8", spec["project"]["pocketos_version"])
        self.assertIn("@sha256:", spec["toolchain"]["miyoo_mini"])
        for profile in ("ci-arm", "ci-zip", "distribution-paused", "host-render"):
            self.assertIn(profile, spec["profiles"])

    def test_distribution_environment_matches_active_state(self):
        spec = load_spec(ROOT / "pocketos" / "environment.json")
        profile = (
            "distribution-paused"
            if spec["project"].get("distribution_state") == "paused"
            else "ci-zip"
        )
        report = check_environment(spec, profile)
        self.assertEqual([], report["failures"]["missing_paths"])
        self.assertEqual([], report["failures"]["source_mismatches"])

    def test_missing_required_tool_marks_environment_incompatible(self):
        spec = {
            "environment_id": "test",
            "version": 1,
            "profiles": {
                "default": {
                    "required_tools": ["definitely-not-a-pocketos-tool"],
                    "required_paths": [],
                    "source_contains": [],
                }
            },
        }
        report = check_environment(spec, "default")
        self.assertEqual("INCOMPATIBLE", report["status"])
        self.assertEqual(["definitely-not-a-pocketos-tool"], report["failures"]["missing_tools"])

    def test_source_contains_mismatch_is_reported(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "file.txt").write_text("actual", encoding="utf-8")
            spec = {
                "environment_id": "test",
                "version": 1,
                "profiles": {
                    "default": {
                        "required_tools": [],
                        "required_paths": ["file.txt"],
                        "source_contains": [{"path": "file.txt", "text": "expected"}],
                    }
                },
            }
            report = check_environment(spec, "default", root)
        self.assertEqual("INCOMPATIBLE", report["status"])
        self.assertEqual(
            [{"path": "file.txt", "reason": "missing text: expected"}],
            report["failures"]["source_mismatches"],
        )

    def test_json_spec_loads_without_comments_or_trailing_commas(self):
        raw = (ROOT / "pocketos" / "environment.json").read_text(encoding="utf-8")
        self.assertIsInstance(json.loads(raw), dict)


if __name__ == "__main__":
    unittest.main()
