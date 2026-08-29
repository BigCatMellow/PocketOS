from pathlib import Path
import re


def sub_once(path, pattern, replacement):
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    text, count = re.subn(pattern, replacement, text, count=1, flags=re.S)
    if count != 1:
        raise SystemExit(f"patch failed in {path}: {pattern[:80]}")
    p.write_text(text, encoding="utf-8")


def replace_once(path, old, new):
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    if old not in text:
        raise SystemExit(f"patch text missing in {path}: {old[:80]!r}")
    p.write_text(text.replace(old, new, 1), encoding="utf-8")


# Health telemetry must scan proc files line-by-line rather than stop at the
# first non-numeric status/meminfo line.
sub_once(
    "src/pocketOS/pocketOS.c",
    r'''static long proc_kb_value\(const char \*path, const char \*label\) \{.*?\n\}\n\nstatic void health_log_sample''',
    '''static long proc_kb_value(const char *path, const char *label) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[256];
    size_t label_len = strlen(label);
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, label, label_len) != 0) continue;
        const char *p = line + label_len;
        while (*p && isspace((unsigned char)*p)) p++;
        errno = 0;
        char *end = NULL;
        long value = strtol(p, &end, 10);
        if (end != p && errno != ERANGE) {
            fclose(f);
            return value;
        }
        break;
    }
    fclose(f);
    return -1;
}

static void health_log_sample''')

# Variant cleanup is now advisory only. It must not mutate the library.
sub_once(
    "tools/installer.py",
    r'''def clean_variants\(folder: Path, log\) -> int:\n.*?\n    return removed\n''',
    '''def clean_variants(folder: Path, log) -> int:
    """Report likely variants without deleting or moving user files."""
    rom_files = [f for f in sorted(folder.iterdir())
                 if f.is_file() and f.suffix.lower() in ROM_EXTS]
    groups: dict = {}
    for f in rom_files:
        groups.setdefault(_base_name(f.stem), []).append(f)
    flagged = 0
    for files in groups.values():
        if len(files) == 1:
            continue
        scored = sorted(files, key=lambda f: (-_rom_score(f.name), f.name))
        log(f"    suggested keep: {scored[0].name}")
        for f in scored[1:]:
            log(f"    possible variant (no action): {f.name}")
            flagged += 1
    return flagged
''')

# CRC is a file checksum, not a checksum of the first 64 MiB.
sub_once(
    "tools/installer.py",
    r'''def _crc32_of\(path: Path\) -> str:\n.*?\n        return ""\n''',
    '''def _crc32_of(path: Path) -> str:
    try:
        crc = 0
        with open(path, "rb") as f:
            while True:
                chunk = f.read(1024 * 1024)
                if not chunk:
                    break
                crc = zlib.crc32(chunk, crc)
        return f"{crc & 0xFFFFFFFF:08X}"
    except Exception:
        return ""
''')

# Mutate existing XML instead of reconstructing it. Parse failure is a hard
# stop for that gamelist, and successful writes are atomic.
sub_once(
    "tools/installer.py",
    r'''def scan_genres_for_system\(roms_root: Path, system_folder: str, db_path: Path, log\) -> int:\n.*?\n    return added\n\n''',
    '''def _write_xml_atomic(tree: ET.ElementTree, dest: Path):
    dest.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp_name = tempfile.mkstemp(prefix=f".{dest.name}.", suffix=".tmp", dir=dest.parent)
    os.close(fd)
    tmp = Path(tmp_name)
    try:
        with tmp.open("wb") as handle:
            tree.write(handle, encoding="utf-8", xml_declaration=True)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(tmp, dest)
    finally:
        if tmp.exists():
            tmp.unlink()


def scan_genres_for_system(roms_root: Path, system_folder: str, db_path: Path, log) -> int:
    system_dir = roms_root / system_folder
    gamelist = system_dir / "miyoogamelist.xml"
    system_name = SYSTEM_MAP.get(system_folder.upper())
    if not system_name:
        return 0
    try:
        db = sqlite3.connect(str(db_path))
    except Exception as e:
        log(f"    DB open failed: {e}")
        return 0

    if gamelist.exists():
        try:
            tree = ET.parse(gamelist)
        except (ET.ParseError, OSError) as e:
            db.close()
            log(f"    ERROR: refusing to overwrite malformed gamelist: {e}")
            return 0
        root = tree.getroot()
    else:
        root = ET.Element("gameList")
        tree = ET.ElementTree(root)

    existing = {}
    for el in root.findall("game"):
        rel = (el.findtext("path") or "").lstrip("./")
        if rel:
            existing[rel] = el

    added = 0
    for rom in sorted(system_dir.iterdir()):
        if rom.suffix.lower() in {".xml", ".db", ".txt", ""} or not rom.is_file():
            continue
        if rom.name in existing:
            continue
        result = _db_lookup(db, rom, system_name)
        name, genre = result if result else (rom.stem, "Unsorted")
        el = ET.SubElement(root, "game")
        ET.SubElement(el, "path").text = "./" + rom.name
        ET.SubElement(el, "name").text = name
        ET.SubElement(el, "genre").text = genre
        added += 1
    db.close()
    if added:
        _write_xml_atomic(tree, gamelist)
    return added

''')
replace_once(
    "tools/installer.py",
    '''    if changed:
        pretty = minidom.parseString(ET.tostring(root, encoding="unicode")).toprettyxml(indent="  ", encoding=None)
        gamelist.write_text(pretty, encoding="utf-8")
    return changed
''',
    '''    if changed:
        _write_xml_atomic(tree, gamelist)
    return changed
''')
replace_once(
    "tools/installer.py",
    '                do_clean = input("  Remove duplicate/bad dumps? [Y/n] ").strip().lower() != "n"\n',
    '                do_clean = input("  Analyze possible duplicate/bad-dump variants? [y/N] ").strip().lower() == "y"\n')
sub_once(
    "tools/installer.py",
    r'''            if do_clean and affected_systems:\n                _head\("── Phase 2b: Removing duplicate/bad dumps ──"\)\n.*?                _ok\(f"Total removed: \{total_removed\}"\)\n''',
    '''            if do_clean and affected_systems:
                _head("── Phase 2b: Analyzing possible variants (no files deleted) ──")
                total_flagged = 0
                for sys_folder in sorted(affected_systems):
                    flagged = clean_variants(roms_root / sys_folder, _log)
                    if flagged:
                        _ok(f"{sys_folder}: flagged {flagged} possible variant(s)")
                        total_flagged += flagged
                _ok(f"Total flagged: {total_flagged}; no ROM files were changed")
''')

# A health report with no valid memory samples is failed evidence, not a pass.
replace_once(
    "tools/pocketos_health_report.py",
    '''    print(f"Battery readings: {span(battery, '%')}")
    print(f"Last event: {rows[-1].get('event', 'unknown')} on {rows[-1].get('screen', rows[-1].get('launcher', 'unknown'))}")
''',
    '''    print(f"Battery readings: {span(battery, '%')}")
    if not rss or not memory:
        missing = []
        if not rss:
            missing.append(rss_key)
        if not memory:
            missing.append("mem_available_kb")
        print("INVALID TELEMETRY: no valid " + ", ".join(missing) + " samples were recorded.")
        return None, 2
    print(f"Last event: {rows[-1].get('event', 'unknown')} on {rows[-1].get('screen', rows[-1].get('launcher', 'unknown'))}")
''')

# Regression tests.
p = Path("tests/test_installer.py")
text = p.read_text(encoding="utf-8")
text = text.replace("import unittest\nfrom pathlib import Path\n", "import unittest\nimport zlib\nfrom pathlib import Path\nfrom unittest.mock import patch\n")
text = text.replace(
    "from tools.installer import audit_install, detect_onion, install_from_dir, uninstall\n",
    "from tools.installer import (\n    _crc32_of, audit_install, clean_variants, detect_onion,\n    install_from_dir, scan_genres_for_system, uninstall,\n)\n")
marker = '\n\nif __name__ == "__main__":\n'
insert = r'''
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
'''
if marker not in text:
    raise SystemExit("test_installer marker missing")
p.write_text(text.replace(marker, insert + marker, 1), encoding="utf-8")

p = Path("tests/test_handheld_ui.py")
text = p.read_text(encoding="utf-8")
marker = '    def test_terminal_stress_test_is_explicit_and_timed(self):\n'
insert = '''    def test_health_monitor_parses_proc_files_line_by_line(self):
        start = self.source.index("static long proc_kb_value")
        end = self.source.index("static void health_log_sample", start)
        parser = self.source[start:end]
        self.assertIn("fgets(line, sizeof(line), f)", parser)
        self.assertIn("strtol(p, &end, 10)", parser)
        self.assertNotIn("fscanf", parser)

'''
if marker not in text:
    raise SystemExit("test_handheld marker missing")
p.write_text(text.replace(marker, insert + marker, 1), encoding="utf-8")

p = Path("tests/test_launch_contract.py")
text = p.read_text(encoding="utf-8")
marker = '\n\nif __name__ == "__main__":\n'
insert = r'''
    def test_onion_preprocess_neutralizes_dollar_command_substitution(self):
        import os
        import tempfile
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            pwned = root / "pwned"
            arg_out = root / "arg.txt"
            launcher = root / "launch.sh"
            launcher.write_text('#!/bin/sh\nprintf "%s" "$1" > "$ARG_OUT"\n')
            launcher.chmod(0o755)
            rom = f"{root}/Game $(touch {pwned}).gba"
            command = f'"{launcher}" "{rom}"'
            parsed = onion_runtime_rompath(command)
            if "$" in parsed:
                command = command.replace("$", r"\$")
            env = os.environ.copy()
            env["ARG_OUT"] = str(arg_out)
            script = root / "cmd_to_run.sh"
            script.write_text(command + "\n")
            subprocess.run(["/bin/sh", str(script)], env=env, check=True)
            self.assertFalse(pwned.exists())
            self.assertEqual(rom, arg_out.read_text())

    def test_shell_metacharacters_inside_onion_quotes_remain_data(self):
        import os
        import tempfile
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            pwned = root / "pwned"
            arg_out = root / "arg.txt"
            launcher = root / "launch.sh"
            launcher.write_text('#!/bin/sh\nprintf "%s" "$1" > "$ARG_OUT"\n')
            launcher.chmod(0o755)
            rom = f"{root}/Game ; touch {pwned} & nope | still.gba"
            command = f'"{launcher}" "{rom}"'
            env = os.environ.copy()
            env["ARG_OUT"] = str(arg_out)
            script = root / "cmd_to_run.sh"
            script.write_text(command + "\n")
            subprocess.run(["/bin/sh", str(script)], env=env, check=True)
            self.assertFalse(pwned.exists())
            self.assertEqual(rom, arg_out.read_text())
'''
if marker not in text:
    raise SystemExit("test_launch marker missing")
p.write_text(text.replace(marker, insert + marker, 1), encoding="utf-8")

Path("tests/test_health_report.py").write_text(r'''import contextlib
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
''', encoding="utf-8")

Path("docs/roadmap-v1.3-reliability.md").write_text('''# PocketOS v1.3 Reliability Roadmap

State: WORKING

## Definition of DONE
PocketOS is safe-by-default on user ROM libraries, preserves Onion metadata, produces trustworthy health evidence, and blocks regressions before release.

Final proof: PR CI + ARM build + adversarial data-integrity tests + independent review + real-device 30-minute stress run with valid memory telemetry.

## Work arc
- [x] Repair health telemetry parsing; invalid telemetry fails visibly.
- [x] Make ROM variant cleanup analysis-only.
- [x] Preserve gamelist XML metadata and refuse malformed overwrite.
- [x] Compute CRC32 across the complete ROM.
- [x] Add PR/main CI and adversarial regressions.
- [ ] Stop invalid SD selection from silently choosing a drive.
- [ ] Consolidate canonical Onion system/folder mappings.
- [ ] Redesign ambiguous/CD/arcade ZIP importing.
- [ ] Replace executable Python overrides with data-only overrides.
- [ ] Stream ZIP extraction with size/free-space/collision limits.
- [ ] Add transactional install/uninstall recovery tests.
- [ ] Run real-device stress validation.
- [ ] Split launcher modules only after behavior is locked by tests.
''', encoding="utf-8")

Path(".github/workflows/ci.yml").write_text('''name: CI

on:
  pull_request:
  push:
    branches: [main, maps-reliability-v1.3]

permissions:
  contents: read

jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - run: python3 -m unittest discover -v
      - run: bash -n tools/pocketos_stress_test.sh tools/onion_baseline_monitor.sh tools/launcher_comparison_monitor.sh

  arm-build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - run: |
          docker run --rm \\
            -v "$PWD:/root/workspace" \\
            aemiii91/miyoomini-toolchain@sha256:e5123590ad75d27f0f4c91196e3119a255cad45f3ae15243e29a8e0a2ec50132 \\
            bash -l -c "source /root/.bashrc; make -C /root/workspace/src/pocketOS clean all"
''', encoding="utf-8")
