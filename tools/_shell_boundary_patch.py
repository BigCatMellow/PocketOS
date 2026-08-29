from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text(encoding="utf-8")
    if old not in text:
        raise SystemExit(f"expected source text not found in {path}")
    target.write_text(text.replace(old, new, 1), encoding="utf-8")


replace_once(
    "src/pocketOS/pocketOS.c",
    "        if (*p == '\"' || *p == '\\n' || *p == '\\r' || *p == '`') return 0;",
    "        if (*p == '\"' || *p == '\\n' || *p == '\\r' || *p == '`' || *p == '\\\\') return 0;",
)

path = Path("tests/test_launch_contract.py")
text = path.read_text(encoding="utf-8")
marker = '\n\nif __name__ == "__main__":\n'
if marker not in text:
    raise SystemExit("launch test insertion marker missing")
if "test_pocketos_writer_rejects_backslash_before_shell_handoff" in text:
    raise SystemExit("backslash launch guard test already present")
insert = r'''
    def test_pocketos_writer_rejects_backslash_before_shell_handoff(self):
        source = (ROOT / "src" / "pocketOS" / "pocketOS.c").read_text()
        start = source.index("static int onion_write_quoted_arg")
        end = source.index("static int write_onion_game_command", start)
        writer = source[start:end]
        self.assertIn("*p == '\\\\'", writer)

    def test_backslash_before_dollar_would_bypass_onion_dollar_escape(self):
        # Document why PocketOS rejects backslashes before handing the command to Onion.
        import os
        import tempfile
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            pwned = root / "pwned"
            arg_out = root / "arg.txt"
            launcher = root / "launch.sh"
            launcher.write_text('#!/bin/sh\nprintf "%s" "$1" > "$ARG_OUT"\n')
            launcher.chmod(0o755)
            rom = f"{root}/Game \\$(touch {pwned}).gba"
            command = f'"{launcher}" "{rom}"'
            parsed = onion_runtime_rompath(command)
            if "$" in parsed:
                command = command.replace("$", r"\$")
            env = os.environ.copy()
            env["ARG_OUT"] = str(arg_out)
            script = root / "cmd_to_run.sh"
            script.write_text(command + "\n")
            subprocess.run(["/bin/sh", str(script)], env=env, check=True)
            # This demonstrates the dangerous shell behavior; the C writer must
            # reject the filename before a command like this can be emitted.
            self.assertTrue(pwned.exists())
'''
path.write_text(text.replace(marker, insert + marker, 1), encoding="utf-8")
