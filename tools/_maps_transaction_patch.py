from pathlib import Path
import re


def replace_once(path, old, new):
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    if old not in text:
        raise SystemExit(f"expected text not found in {path}: {old[:120]!r}")
    p.write_text(text.replace(old, new, 1), encoding="utf-8")


def sub_once(path, pattern, replacement):
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    text, count = re.subn(pattern, replacement, text, count=1, flags=re.S)
    if count != 1:
        raise SystemExit(f"pattern failed in {path}: {pattern[:120]!r}")
    p.write_text(text, encoding="utf-8")


# Transaction helper: snapshot only PocketOS-owned paths plus the Onion runtime
# files PocketOS mutates. Never back up or replace broad Onion directories.
p = Path("tools/installer.py")
text = p.read_text(encoding="utf-8")
anchor = '''def _replace_tree(src: Path, dest: Path, preserve=()):
'''
transaction = r'''POCKETOS_TRANSACTION_PATHS = (
    Path(".tmp_update/res/pocketos"),
    Path(".tmp_update/bin/pocketOS"),
    Path("pocketos-health-report.py"),
    Path("pocketos-stress-test.sh"),
    Path("onion-baseline-monitor.sh"),
    Path("launcher-comparison-monitor.sh"),
    Path(".tmp_update/runtime.sh"),
    Path(".tmp_update/runtime.sh.before-pocketos"),
)


def _remove_snapshot_target(path: Path):
    if path.is_symlink() or path.is_file():
        path.unlink()
    elif path.is_dir():
        shutil.rmtree(path)


class _InstallTransaction:
    """Rollback guard for the small set of paths PocketOS owns or mutates."""

    def __init__(self, sd: Path):
        self.sd = sd
        tmp_root = sd / ".tmp_update"
        tmp_root.mkdir(parents=True, exist_ok=True)
        self.root = Path(tempfile.mkdtemp(prefix=".pocketos-transaction-", dir=tmp_root))
        self.entries = []
        self.parent_state = {
            sd / ".tmp_update" / "bin": (sd / ".tmp_update" / "bin").exists(),
            sd / ".tmp_update" / "res": (sd / ".tmp_update" / "res").exists(),
        }
        for index, relative in enumerate(POCKETOS_TRANSACTION_PATHS):
            target = sd / relative
            existed = target.exists() or target.is_symlink()
            backup = self.root / str(index)
            kind = None
            link_target = None
            if existed:
                if target.is_symlink():
                    kind = "symlink"
                    link_target = os.readlink(target)
                elif target.is_dir():
                    kind = "dir"
                    shutil.copytree(target, backup, symlinks=True)
                else:
                    kind = "file"
                    shutil.copy2(target, backup, follow_symlinks=False)
            self.entries.append((target, existed, kind, backup, link_target))

    def rollback(self):
        for target, existed, kind, backup, link_target in reversed(self.entries):
            if target.exists() or target.is_symlink():
                _remove_snapshot_target(target)
            if not existed:
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            if kind == "dir":
                shutil.copytree(backup, target, symlinks=True)
            elif kind == "symlink":
                os.symlink(link_target, target)
            else:
                shutil.copy2(backup, target, follow_symlinks=False)
        for parent, existed in self.parent_state.items():
            if not existed and parent.is_dir():
                try:
                    parent.rmdir()
                except OSError:
                    pass
        self._cleanup()

    def commit(self):
        self._cleanup()

    def _cleanup(self):
        if self.root.exists():
            shutil.rmtree(self.root)


def _remove_owned_file(path: Path, log, label: str):
    if path.exists() or path.is_symlink():
        path.unlink()
        log(f"  Removed {label}")


'''
if anchor not in text:
    raise SystemExit("installer helper anchor missing")
text = text.replace(anchor, transaction + anchor, 1)
p.write_text(text, encoding="utf-8")

# Wrap the complete installation mutation set in the transaction.
sub_once(
    "tools/installer.py",
    r'''def install_from_dir\(src: Path, sd: Path, log\):\n.*?\n        raise RuntimeError\("Post-install audit failed: " \+ "; "\.join\(errors\)\)\n''',
    '''def install_from_dir(src: Path, sd: Path, log):
    errors, warnings = audit_install(sd, src)
    for warning in warnings:
        log(f"  WARNING: {warning}")
    if errors:
        raise RuntimeError("; ".join(errors))

    bin_src = src / ".tmp_update" / "bin" / "pocketOS"
    res_src = src / ".tmp_update" / "res" / "pocketos"
    bin_dest = sd / ".tmp_update" / "bin"
    res_dest = sd / ".tmp_update" / "res" / "pocketos"
    report_dest = sd / "pocketos-health-report.py"
    stress_dest = sd / "pocketos-stress-test.sh"
    onion_monitor_dest = sd / "onion-baseline-monitor.sh"
    comparison_monitor_dest = sd / "launcher-comparison-monitor.sh"
    if not bin_src.exists():
        raise FileNotFoundError(f"Binary not found: {bin_src}")
    if not res_src.is_dir():
        raise FileNotFoundError(f"Assets not found: {res_src}")

    transaction = _InstallTransaction(sd)
    try:
        log("  Setting up folders on SD card...")
        bin_dest.mkdir(parents=True, exist_ok=True)
        log("  Copying themes, icons, and fonts...")
        _replace_tree(res_src, res_dest, preserve=(Path("theme.json"),))
        if PAYLOAD_HEALTH_REPORT.is_file():
            _copy_file_atomic(PAYLOAD_HEALTH_REPORT, report_dest)
        if PAYLOAD_STRESS_TEST.is_file():
            _copy_file_atomic(PAYLOAD_STRESS_TEST, stress_dest)
            stress_dest.chmod(0o755)
        if PAYLOAD_ONION_MONITOR.is_file():
            _copy_file_atomic(PAYLOAD_ONION_MONITOR, onion_monitor_dest)
            onion_monitor_dest.chmod(0o755)
        if PAYLOAD_COMPARISON_MONITOR.is_file():
            _copy_file_atomic(PAYLOAD_COMPARISON_MONITOR, comparison_monitor_dest)
            comparison_monitor_dest.chmod(0o755)
        log("  Copying PocketOS launcher...")
        _copy_file_atomic(bin_src, bin_dest / "pocketOS")
        (bin_dest / "pocketOS").chmod(0o755)
        log("  Installing fail-open Onion launcher hook...")
        install_runtime_hook(sd)
        errors, _warnings = audit_install(sd, src)
        if errors:
            raise RuntimeError("Post-install audit failed: " + "; ".join(errors))
    except Exception:
        transaction.rollback()
        raise
    else:
        transaction.commit()
''')

# Uninstall is also transactional and now removes all root-level PocketOS helper
# scripts that installation owns.
sub_once(
    "tools/installer.py",
    r'''def uninstall\(sd: Path, log\):\n.*?\n        log\("  Removed themes, icons, and fonts"\)\n''',
    '''def uninstall(sd: Path, log):
    transaction = _InstallTransaction(sd)
    try:
        log("  Restoring the stock Onion launcher...")
        remove_runtime_hook(sd)
        log("  Removing PocketOS launcher...")
        _remove_owned_file(sd / ".tmp_update" / "bin" / "pocketOS", log, "launcher binary")
        log("  Removing themes and assets...")
        res = sd / ".tmp_update" / "res" / "pocketos"
        if res.exists():
            shutil.rmtree(res)
            log("  Removed themes, icons, and fonts")
        for filename, label in (
            ("pocketos-health-report.py", "health report helper"),
            ("pocketos-stress-test.sh", "stress-test helper"),
            ("onion-baseline-monitor.sh", "Onion baseline monitor"),
            ("launcher-comparison-monitor.sh", "launcher comparison monitor"),
        ):
            _remove_owned_file(sd / filename, log, label)
    except Exception:
        transaction.rollback()
        raise
    else:
        transaction.commit()
''')

# Runtime backup must track the stock runtime being patched, not the first Onion
# version PocketOS ever saw. Refresh it whenever the current runtime is unpatched.
p = Path("tools/onion_runtime.py")
text = p.read_text(encoding="utf-8")
old = '''    backup = Path(sd_root) / BACKUP_REL
    if not backup.exists():
        shutil.copy2(runtime, backup)
    _atomic_write(runtime, patched)
'''
new = '''    backup = Path(sd_root) / BACKUP_REL
    # This call is patching a stock/unmanaged runtime, so it is the most current
    # rollback source. Refresh atomically instead of retaining a stale Onion copy.
    backup_tmp = backup.with_name(f".{backup.name}.installing")
    shutil.copy2(runtime, backup_tmp)
    os.replace(backup_tmp, backup)
    _atomic_write(runtime, patched)
'''
if old not in text:
    raise SystemExit("runtime backup block missing")
p.write_text(text.replace(old, new, 1), encoding="utf-8")

# Regression tests: compare exact owned-state snapshots before and after injected
# failures, and prove runtime backup refreshes after an Onion runtime replacement.
p = Path("tests/test_installer.py")
text = p.read_text(encoding="utf-8")
text = text.replace(
    '''from tools.installer import (
    _crc32_of, _select_candidate, audit_install, clean_variants, detect_onion,
    install_from_dir, scan_genres_for_system, uninstall,
)
''',
    '''from tools.installer import (
    POCKETOS_TRANSACTION_PATHS, _crc32_of, _select_candidate, audit_install,
    clean_variants, detect_onion, install_from_dir, scan_genres_for_system, uninstall,
)
''',
    1,
)
class_anchor = '''class InstallerTests(unittest.TestCase):
'''
helper = r'''class InstallerTests(unittest.TestCase):
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

'''
if class_anchor not in text:
    raise SystemExit("installer test class anchor missing")
text = text.replace(class_anchor, helper, 1)
marker = '\n\nif __name__ == "__main__":\n'
insert = r'''
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
'''
if marker not in text:
    raise SystemExit("installer test marker missing")
text = text.replace(marker, insert + marker, 1)
p.write_text(text, encoding="utf-8")

p = Path("tests/test_onion_runtime.py")
text = p.read_text(encoding="utf-8")
marker = '\n\nif __name__ == "__main__":\n'
insert = r'''
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
'''
if marker not in text:
    raise SystemExit("runtime test marker missing")
p.write_text(text.replace(marker, insert + marker, 1), encoding="utf-8")

p = Path("docs/roadmap-v1.3-reliability.md")
text = p.read_text(encoding="utf-8")
text = text.replace(
    "- [ ] Complete transactional install/uninstall recovery tests.\n",
    "- [x] Complete transactional install/uninstall recovery tests.\n",
    1,
)
if "- [x] Refresh Onion runtime backup when a newer stock runtime is patched." not in text:
    text += "\n- [x] Refresh Onion runtime backup when a newer stock runtime is patched.\n"
p.write_text(text, encoding="utf-8")
