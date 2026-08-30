import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class ReleaseContractTests(unittest.TestCase):
    def test_release_builds_complete_onedir_installer(self):
        workflow = (ROOT / ".github" / "workflows" / "release.yml").read_text()
        self.assertIn("python3 tools/check_environment.py --profile ci-arm --json", workflow)
        self.assertIn("python3 tools/check_environment.py --profile ci-zip --json", workflow)
        self.assertIn(
            "aemiii91/miyoomini-toolchain@sha256:e5123590ad75d27f0f4c91196e3119a255cad45f3ae15243e29a8e0a2ec50132",
            workflow,
        )
        self.assertIn("name: Render UI contract", workflow)
        self.assertIn("python3 tools/check_environment.py --profile host-render --json", workflow)
        self.assertIn("python3 tools/render_handheld_ui.py --output build/handheld-ui-ci", workflow)
        self.assertIn("needs: [build-arm, render-ui]", workflow)
        self.assertIn("python3 -m unittest discover -v", workflow)
        self.assertIn("pyinstaller --onedir --console", workflow)
        self.assertIn('--add-data "payload${{ matrix.sep }}payload"', workflow)
        self.assertIn("name: Build ROM importer", workflow)
        self.assertIn("tools/rom_importer.py", workflow)
        self.assertIn("PocketOS-ROMImporter-windows.exe", workflow)
        self.assertIn("PocketOS-ROMImporter-linux.tar.gz", workflow)
        self.assertIn("-name 'PocketOS-ROMImporter-*'", workflow)
        self.assertIn("-name 'PocketOS-Installer-*'", workflow)
        self.assertIn("-name 'PocketOS-GenreScanner-*'", workflow)
        self.assertIn("cp tools/install_runtime.py staging/install-pocketos.py", workflow)
        self.assertIn("cp tools/onion_runtime.py staging/onion_runtime.py", workflow)
        self.assertIn('cp -r "assets/App/PocketOS Test Center" staging/App/', workflow)
        self.assertIn('cp -r "assets/App/PocketOS Test Center" payload/App/', workflow)
        self.assertIn("xargs -0 sha256sum > manifest.sha256", workflow)
        self.assertIn("sha256sum -c manifest.sha256", workflow)
        self.assertIn("1980-01-01 00:00:00 UTC", workflow)
        self.assertIn("zip -X -q", workflow)
        self.assertIn('unzip -tq "pocketOS-${{ github.ref_name }}.zip"', workflow)
        self.assertIn('pocketOS-${{ github.ref_name }}.zip.sha256', workflow)

    def test_installer_spec_uses_same_payload_contract(self):
        spec = (ROOT / "tools" / "PocketOS Installer.spec").read_text()
        self.assertIn("project = Path(SPECPATH).parent", spec)
        self.assertIn("payload = project / 'payload'", spec)
        self.assertIn("str(project / 'tools' / 'installer.py')", spec)
        self.assertIn("str(payload / '.tmp_update' / 'bin' / 'pocketOS')", spec)
        self.assertIn("payload/.tmp_update/res/pocketos", spec)
        self.assertNotIn("pocketOS-v1.0", spec)

    def test_legacy_pywebview_installer_ui_is_removed(self):
        self.assertFalse((ROOT / "tools" / "ui").exists())


if __name__ == "__main__":
    unittest.main()
