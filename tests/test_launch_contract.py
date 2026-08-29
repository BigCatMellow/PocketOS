import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def onion_runtime_rompath(command: str) -> str:
    awk = '{ st = index($0,"\\\" \\\""); print substr($0,st+3,length($0)-st-3)}'
    result = subprocess.run(
        ["awk", awk], input=command, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True,
    )
    return result.stdout.rstrip("\n")


class LaunchContractTests(unittest.TestCase):
    def test_double_quoted_command_matches_onion_parser(self):
        rom = "/mnt/SDCARD/Roms/GBA/Friday Night Funkin' GBA.gba"
        command = (
            'LD_PRELOAD=/mnt/SDCARD/miyoo/app/../lib/libpadsp.so '
            '"/mnt/SDCARD/Emu/GBA/launch.sh" '
            f'"{rom}"'
        )
        self.assertEqual(rom, onion_runtime_rompath(command))

    def test_pocketos_writer_uses_onion_double_quote_contract(self):
        source = (ROOT / "src" / "pocketOS" / "pocketOS.c").read_text()
        self.assertIn("write_onion_game_command", source)
        self.assertIn("onion_write_quoted_arg", source)
        self.assertNotIn("shell_write_quoted", source)

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


if __name__ == "__main__":
    unittest.main()
