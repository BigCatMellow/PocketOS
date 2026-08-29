from pathlib import Path


def replace_once(path, old, new):
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    if old not in text:
        raise SystemExit(f"expected text not found in {path}: {old[:100]!r}")
    p.write_text(text.replace(old, new, 1), encoding="utf-8")


replace_once(
    "tools/installer.py",
    '''def _log(text):
    t = text.strip()
    if not t:
        print()
    elif t.startswith("✓"):
        _ok(t[1:].strip())
    elif t.startswith("✗") or "ERROR" in text:
        _err(t)
    elif t.startswith("──"):
        _head(t)
    else:
        _info(t)


class Installer:
''',
    '''def _log(text):
    t = text.strip()
    if not t:
        print()
    elif t.startswith("✓"):
        _ok(t[1:].strip())
    elif t.startswith("✗") or "ERROR" in text:
        _err(t)
    elif t.startswith("──"):
        _head(t)
    else:
        _info(t)


def _select_candidate(candidates, input_fn=input, warn_fn=_warn):
    """Require an explicit valid choice; never infer a destructive target."""
    while True:
        raw = input_fn(f"\\n  Select [1-{len(candidates)}]: ").strip()
        try:
            number = int(raw)
        except ValueError:
            number = 0
        if 1 <= number <= len(candidates):
            return candidates[number - 1]
        warn_fn(f"Enter a number from 1 to {len(candidates)}")


class Installer:
''')

replace_once(
    "tools/installer.py",
    '''            choice = input(f"\\n  Select [1-{len(candidates)}]: ").strip()
            try:
                self._sd = candidates[int(choice) - 1]
            except (ValueError, IndexError):
                self._sd = candidates[0]
            _ok(f"Selected: {_BOLD}{self._sd}{_R}")
''',
    '''            self._sd = _select_candidate(candidates)
            _ok(f"Selected: {_BOLD}{self._sd}{_R}")
''')

p = Path("tests/test_installer.py")
text = p.read_text(encoding="utf-8")
text = text.replace(
    "    _crc32_of, audit_install, clean_variants, detect_onion,\n",
    "    _crc32_of, _select_candidate, audit_install, clean_variants, detect_onion,\n",
    1,
)
marker = '\n\nif __name__ == "__main__":\n'
insert = r'''
    def test_sd_candidate_selection_reprompts_until_valid(self):
        choices = iter(["nonsense", "0", "3", "2"])
        warnings = []
        candidates = [Path("/card-one"), Path("/card-two")]
        selected = _select_candidate(candidates, lambda _prompt: next(choices), warnings.append)
        self.assertEqual(Path("/card-two"), selected)
        self.assertEqual(3, len(warnings))
        self.assertTrue(all("1 to 2" in warning for warning in warnings))
'''
if marker not in text:
    raise SystemExit("test marker not found")
p.write_text(text.replace(marker, insert + marker, 1), encoding="utf-8")

replace_once(
    "docs/roadmap-v1.3-reliability.md",
    "- [ ] Stop invalid SD selection from silently choosing a drive.\n",
    "- [x] Stop invalid SD selection from silently choosing a drive.\n",
)
