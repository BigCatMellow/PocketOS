#!/usr/bin/env python3
"""Build and render PocketOS views against a fake Onion SD card."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import sqlite3
import struct
import subprocess


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "src" / "pocketOS" / "pocketOS.c"
FONT = Path("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf")
THEME_SOURCE = ROOT / "assets" / "res" / "pocketos"
THEME_VARIANTS = tuple(path.stem.removeprefix("theme_")
                       for path in sorted(THEME_SOURCE.glob("theme_*.json")))
SCREENS = (
    "most", "browse", "library", "favorites", "settings",
    "apps", "settings-list", "font", "theme", "device", "about",
    "recent", "options", "rom-info", "save-info",
)

SYSTEMS = (
    ("FC", "Nintendo Entertainment System", "nes"),
    ("SFC", "Super Nintendo", "sfc"),
    ("GB", "Game Boy", "gb"),
    ("GBC", "Game Boy Color", "gbc"),
    ("GBA", "Game Boy Advance", "gba"),
    ("MD", "Mega Drive", "md"),
    ("PS", "PlayStation", "chd"),
    ("ARCADE", "Arcade", "zip"),
)

GAMES = (
    "Chrono Trigger",
    "Castlevania: Aria of Sorrow",
    "The Legend of Zelda: Link's Awakening",
    "Metroid Fusion",
    "Advance Wars",
    "Final Fantasy VI",
    "Sonic the Hedgehog 2",
)

GENRES = ("RPG", "Action/Adventure", "Platformer", "Strategy", "Racing", "Puzzle")


def create_fixture(sd: Path) -> None:
    if sd.exists():
        shutil.rmtree(sd)
    (sd / ".tmp_update" / "logs").mkdir(parents=True)
    (sd / ".tmp_update" / "res" / "pocketos").mkdir(parents=True)
    (sd / ".tmp_update" / "onionVersion").mkdir(parents=True)
    (sd / "Saves" / "CurrentProfile" / "play_activity").mkdir(parents=True)
    (sd / "miyoo" / "app").mkdir(parents=True)
    (sd / "Roms").mkdir()
    (sd / "Emu").mkdir()
    shutil.copy2(FONT, sd / "miyoo" / "app" / FONT.name)
    (sd / ".tmp_update" / "onionVersion" / "version.txt").write_text(
        "v4.3.1-1", encoding="utf-8"
    )
    (sd / "firmwareVersion").write_text("202310271401", encoding="utf-8")

    theme_dir = sd / ".tmp_update" / "res" / "pocketos"
    for source in sorted(THEME_SOURCE.glob("theme_*.json")):
        shutil.copy2(source, theme_dir / source.name)

    favorite_lines = []
    activity_rows = []
    rom_id = 1

    for system_index, (label, _full_name, extension) in enumerate(SYSTEMS):
        emu = sd / "Emu" / label
        roms = sd / "Roms" / label
        emu.mkdir()
        roms.mkdir()
        (emu / "config.json").write_text(
            json.dumps({
                "label": label,
                "rompath": f"../../Roms/{label}",
                "extlist": extension,
            }),
            encoding="utf-8",
        )
        (emu / "launch.sh").write_text("#!/bin/sh\n", encoding="utf-8")

        xml_lines = ["<gameList>"]
        for game_index, title in enumerate(GAMES):
            display = title if system_index == 0 else f"{title} {label}"
            filename = f"{display}.{extension}"
            rom = roms / filename
            rom.touch()
            genre = GENRES[(system_index + game_index) % len(GENRES)]
            xml_lines.append(
                f"  <game><path>./{filename}</path><name>{display}</name>"
                f"<genre>{genre}</genre></game>"
            )
            activity_rows.append((rom_id, display, f"{label}/{filename}", 2700 + rom_id * 713))
            if len(favorite_lines) < 8 and game_index == 0:
                favorite_lines.append(json.dumps({
                    "label": display,
                    "rompath": str(rom),
                    "launch": str(emu / "launch.sh"),
                }))
            rom_id += 1
        xml_lines.append("</gameList>")
        (roms / "miyoogamelist.xml").write_text("\n".join(xml_lines) + "\n", encoding="utf-8")

    (sd / "Roms" / "favourite.json").write_text(
        "\n".join(favorite_lines) + "\n", encoding="utf-8"
    )
    (sd / "Roms" / "recentlist.json").write_text(
        "\n".join(reversed(favorite_lines)) + "\n", encoding="utf-8"
    )

    db_path = sd / "Saves" / "CurrentProfile" / "play_activity" / "play_activity_db.sqlite"
    with sqlite3.connect(db_path) as db:
        db.execute("CREATE TABLE rom (id INTEGER PRIMARY KEY, name TEXT, file_path TEXT)")
        db.execute("CREATE TABLE play_activity (rom_id INTEGER, play_time INTEGER)")
        db.executemany(
            "INSERT INTO rom(id, name, file_path) VALUES (?, ?, ?)",
            ((row[0], row[1], row[2]) for row in activity_rows),
        )
        db.executemany(
            "INSERT INTO play_activity(rom_id, play_time) VALUES (?, ?)",
            ((row[0], row[3]) for row in activity_rows),
        )


def compile_launcher(binary: Path, sd: Path) -> None:
    if not FONT.is_file():
        raise RuntimeError(f"host smoke-test font is missing: {FONT}")
    sdl_flags = subprocess.check_output(["sdl-config", "--cflags", "--libs"], text=True).split()
    command = [
        "gcc", "-std=gnu99", "-Wall", "-Wextra", "-Werror", "-DPOCKETOS_PC_TEST",
        f'-DPOCKETOS_ROOT="{sd}"',
        f'-DCMD_PATH="{binary.parent / "cmd_to_run.sh"}"',
        f'-DFONT_PATH="{FONT}"', f'-DFONT_ALT="{FONT}"', f'-DFONT_PRIMARY="{FONT}"',
        f'-DFIRMWARE_VERSION_PATH="{sd / "firmwareVersion"}"',
        str(SOURCE), "-o", str(binary), *sdl_flags,
        "-lSDL_image", "-lSDL_ttf", "-Wl,-l:libsqlite3.so.0", "-lz", "-lm",
    ]
    subprocess.run(command, check=True)


def bmp_stats(path: Path) -> tuple[int, int, int]:
    data = path.read_bytes()
    if data[:2] != b"BM" or len(data) < 54:
        raise RuntimeError(f"invalid BMP screenshot: {path}")
    pixel_offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bpp = struct.unpack_from("<H", data, 28)[0]
    if (width, abs(height)) != (640, 480) or bpp not in (24, 32):
        raise RuntimeError(f"unexpected screenshot geometry: {width}x{height}x{bpp}")
    pixels = data[pixel_offset:]
    pixel_bytes = bpp // 8
    colors = {
        pixels[index:index + pixel_bytes]
        for index in range(0, len(pixels) - pixel_bytes + 1, pixel_bytes)
    }
    return width, abs(height), len(colors)


def render(binary: Path, output: Path) -> None:
    hashes = set()
    for screen in SCREENS:
        bmp = output / f"{screen}.bmp"
        env = os.environ.copy()
        env.update({
            "SDL_VIDEODRIVER": "dummy",
            "SDL_AUDIODRIVER": "dummy",
            "POCKETOS_AUTOTEST_FRAMES": "2",
            "POCKETOS_START_SCREEN": screen,
            "POCKETOS_SCREENSHOT_PATH": str(bmp),
        })
        subprocess.run([str(binary)], cwd=output, env=env, check=True)
        _width, _height, colors = bmp_stats(bmp)
        if colors < 12:
            raise RuntimeError(f"{screen} render appears blank ({colors} colors)")
        digest = __import__("hashlib").sha256(bmp.read_bytes()).hexdigest()
        if digest in hashes:
            raise RuntimeError(f"{screen} duplicated another primary view")
        hashes.add(digest)
        png = output / f"{screen}.png"
        subprocess.run(["convert", str(bmp), str(png)], check=True)
        print(f"{screen:13s}  colors={colors:4d}  {png}")

    theme_hashes = set()
    theme_dir = output / "sd" / ".tmp_update" / "res" / "pocketos"
    for theme in THEME_VARIANTS:
        shutil.copy2(theme_dir / f"theme_{theme}.json", theme_dir / "theme.json")
        name = f"theme-{theme}"
        bmp = output / f"{name}.bmp"
        env = os.environ.copy()
        env.update({
            "SDL_VIDEODRIVER": "dummy",
            "SDL_AUDIODRIVER": "dummy",
            "POCKETOS_AUTOTEST_FRAMES": "2",
            "POCKETOS_START_SCREEN": "settings",
            "POCKETOS_SCREENSHOT_PATH": str(bmp),
        })
        subprocess.run([str(binary)], cwd=output, env=env, check=True)
        _width, _height, colors = bmp_stats(bmp)
        if colors < 12:
            raise RuntimeError(f"{name} render appears blank ({colors} colors)")
        digest = __import__("hashlib").sha256(bmp.read_bytes()).hexdigest()
        if digest in theme_hashes:
            raise RuntimeError(f"{name} duplicated another legacy theme")
        theme_hashes.add(digest)
        png = output / f"{name}.png"
        subprocess.run(["convert", str(bmp), str(png)], check=True)
        print(f"{name:13s}  colors={colors:4d}  {png}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--output",
        type=Path,
        default=ROOT / "build" / "handheld-ui",
        help="directory for the host binary, fixture, and screenshots",
    )
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    sd = output / "sd"
    binary = output / "pocketOS-host"
    create_fixture(sd)
    compile_launcher(binary, sd)
    render(binary, output)


if __name__ == "__main__":
    main()
