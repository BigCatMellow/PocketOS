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


if __name__ == "__main__":
    unittest.main()
