import subprocess
import shutil
import tempfile
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


def compiled_pocketos_command(launch: str, rom: str) -> subprocess.CompletedProcess:
    """Compile and execute the command writer taken from the production C file."""
    compiler = shutil.which("cc")
    if not compiler:
        raise unittest.SkipTest("a C compiler is required for the launch contract")
    source = (ROOT / "src" / "pocketOS" / "pocketOS.c").read_text(encoding="utf-8")
    start = source.index("static int onion_write_quoted_arg")
    end = source.index("static int json_int_file", start)
    writer = source[start:end]
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        harness = root / "writer.c"
        binary = root / "writer"
        harness.write_text(
            "#include <stdio.h>\n" + writer + "\n"
            "int main(int argc, char **argv) {\n"
            "  return argc == 3 && write_onion_game_command(stdout, argv[1], argv[2]) ? 0 : 2;\n"
            "}\n",
            encoding="utf-8",
        )
        subprocess.run([compiler, "-Wall", "-Werror", "-o", str(binary), str(harness)], check=True)
        return subprocess.run([str(binary), launch, rom], text=True, capture_output=True, check=False)


class LaunchContractTests(unittest.TestCase):
    def test_double_quoted_command_matches_onion_parser(self):
        rom = "/mnt/SDCARD/Roms/GBA/Friday Night Funkin' GBA.gba"
        command = (
            'LD_PRELOAD=/mnt/SDCARD/miyoo/app/../lib/libpadsp.so '
            '"/mnt/SDCARD/Emu/GBA/launch.sh" '
            f'"{rom}"'
        )
        self.assertEqual(rom, onion_runtime_rompath(command))

    def test_production_writer_round_trips_shell_special_filenames(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            pwned = root / "pwned"
            arg_out = root / "arg.txt"
            launcher = root / "launch.sh"
            launcher.write_text('#!/bin/sh\nprintf "%s" "$1" > "$ARG_OUT"\n')
            launcher.chmod(0o755)
            rom = (
                f"{root}/name with spaces 'single' $dollar $(touch {pwned}) "
                "; semi & amp | pipe (paren) [bracket] üñïçødé.gba"
            )
            result = compiled_pocketos_command(str(launcher), rom)
            self.assertEqual(0, result.returncode, result.stderr)
            command = result.stdout
            parsed = onion_runtime_rompath(command)
            self.assertEqual(rom, parsed)
            # Onion converts each dollar to a literal dollar before this script runs.
            script = root / "cmd_to_run.sh"
            script.write_text(command.replace("$", r"\$") + "\n", encoding="utf-8")
            env = {"ARG_OUT": str(arg_out)}
            subprocess.run(["/bin/sh", str(script)], env=env, check=True)
            self.assertFalse(pwned.exists())
            self.assertEqual(rom, arg_out.read_text(encoding="utf-8"))

    def test_production_writer_rejects_shell_escaping_boundary_characters(self):
        for value in ('quote".gba', 'backtick`.gba', 'backslash\\.gba', 'newline\n.gba'):
            with self.subTest(value=value):
                result = compiled_pocketos_command("/tmp/launch.sh", value)
                self.assertEqual(2, result.returncode)

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
