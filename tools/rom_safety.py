"""Shared data-integrity helpers for PocketOS ROM desktop tools."""

from __future__ import annotations

import os
import shutil
import tempfile
import xml.etree.ElementTree as ET
import zipfile
import zlib
from pathlib import Path, PurePosixPath

ARCHIVE_MAX_EXPANDED_BYTES = 8 * 1024 * 1024 * 1024
ARCHIVE_FREE_SPACE_RESERVE = 64 * 1024 * 1024
COPY_CHUNK_BYTES = 1024 * 1024


class GamelistError(RuntimeError):
    pass


class ArchiveSafetyError(RuntimeError):
    pass


def crc32_of(path: Path) -> str:
    """Return the CRC32 of a regular ROM file, streaming the complete file.

    ZIP containers deliberately fall back to filename matching because the
    archive CRC is not the raw-ROM CRC and choosing an arbitrary member can
    silently identify the wrong game.
    """
    if path.suffix.lower() == ".zip":
        return ""
    try:
        crc = 0
        with path.open("rb") as handle:
            while True:
                chunk = handle.read(COPY_CHUNK_BYTES)
                if not chunk:
                    break
                crc = zlib.crc32(chunk, crc)
        return f"{crc & 0xFFFFFFFF:08X}"
    except OSError:
        return ""


def load_gamelist_tree(path: Path) -> ET.ElementTree:
    """Load a gamelist without discarding unknown metadata.

    Missing files produce a new tree. Existing malformed files are never treated
    as empty because that would make a later save destructive.
    """
    if not path.exists():
        return ET.ElementTree(ET.Element("gameList"))
    try:
        parser = ET.XMLParser(target=ET.TreeBuilder(insert_comments=True))
        return ET.parse(path, parser=parser)
    except (ET.ParseError, OSError) as exc:
        raise GamelistError(f"refusing to overwrite malformed gamelist {path}: {exc}") from exc


def index_games(root: ET.Element) -> dict[str, ET.Element]:
    result: dict[str, ET.Element] = {}
    for game in root.findall("game"):
        rel = (game.findtext("path") or "").lstrip("./")
        if rel:
            result[rel] = game
    return result


def write_xml_atomic(tree: ET.ElementTree, dest: Path) -> None:
    """Atomically replace a gamelist, retaining the prior version as .bak."""
    dest.parent.mkdir(parents=True, exist_ok=True)
    if dest.exists():
        backup = dest.with_name(f"{dest.name}.bak")
        backup_tmp = backup.with_name(f".{backup.name}.tmp")
        shutil.copy2(dest, backup_tmp)
        os.replace(backup_tmp, backup)
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
        try:
            tmp.unlink()
        except FileNotFoundError:
            pass


def _safe_member_name(filename: str) -> str:
    normalized = filename.replace("\\", "/")
    name = PurePosixPath(normalized).name
    if not name or name in {".", ".."}:
        raise ArchiveSafetyError(f"unsafe archive member name: {filename!r}")
    return name


def extract_zip_roms(
    zip_path: Path,
    dest_folder: Path,
    allowed_extensions: set[str] | frozenset[str],
    log,
) -> list[Path]:
    """Stream approved ROM members into an installed Onion system folder.

    The archive is preflighted before any writes: expanded-size budget, free
    space, and basename collisions must all pass. Existing files are preserved.
    Each new file is written to a temporary sibling and atomically renamed.
    """
    allowed = {ext.lower() for ext in allowed_extensions}
    extracted: list[Path] = []

    try:
        with zipfile.ZipFile(zip_path) as archive:
            selected: list[tuple[zipfile.ZipInfo, str]] = []
            seen_names: set[str] = set()
            expanded = 0

            for info in archive.infolist():
                if info.is_dir():
                    continue
                out_name = _safe_member_name(info.filename)
                if Path(out_name).suffix.lower() not in allowed:
                    continue
                key = out_name.casefold()
                if key in seen_names:
                    raise ArchiveSafetyError(
                        f"archive contains multiple ROM members named {out_name!r}; nothing extracted"
                    )
                seen_names.add(key)
                expanded += info.file_size
                if expanded > ARCHIVE_MAX_EXPANDED_BYTES:
                    raise ArchiveSafetyError("archive expanded size exceeds safety limit")
                selected.append((info, out_name))

            if not selected:
                return []

            free = shutil.disk_usage(dest_folder).free
            if expanded + ARCHIVE_FREE_SPACE_RESERVE > free:
                raise ArchiveSafetyError(
                    f"not enough free space: need {expanded + ARCHIVE_FREE_SPACE_RESERVE} bytes, have {free}"
                )

            for info, out_name in selected:
                out_path = dest_folder / out_name
                if out_path.exists():
                    log(f"  SKIP (already exists): {out_name}")
                    continue

                fd, tmp_name = tempfile.mkstemp(
                    prefix=f".{out_name}.", suffix=".pocketos-part", dir=dest_folder
                )
                os.close(fd)
                tmp = Path(tmp_name)
                try:
                    with archive.open(info, "r") as src, tmp.open("wb") as dst:
                        shutil.copyfileobj(src, dst, length=COPY_CHUNK_BYTES)
                        dst.flush()
                        os.fsync(dst.fileno())
                    os.replace(tmp, out_path)
                finally:
                    try:
                        tmp.unlink()
                    except FileNotFoundError:
                        pass
                extracted.append(out_path)
                log(f"  extracted: {out_name}")

    except (OSError, zipfile.BadZipFile, ArchiveSafetyError) as exc:
        log(f"  ERROR reading {zip_path.name}: {exc}")
        return []

    return extracted


def copy_file_atomic_new(source: Path, dest_folder: Path, log) -> list[Path]:
    """Copy a user-selected ROM intact without replacing an existing file."""
    destination = dest_folder / source.name
    if destination.exists():
        log(f"  SKIP (already exists): {source.name}")
        return []
    fd, tmp_name = tempfile.mkstemp(prefix=f".{source.name}.", suffix=".pocketos-part", dir=dest_folder)
    os.close(fd)
    tmp = Path(tmp_name)
    try:
        with source.open("rb") as src, tmp.open("wb") as dst:
            shutil.copyfileobj(src, dst, length=COPY_CHUNK_BYTES)
            dst.flush()
            os.fsync(dst.fileno())
        os.replace(tmp, destination)
    except OSError as exc:
        log(f"  ERROR copying {source.name}: {exc}")
        return []
    finally:
        try:
            tmp.unlink()
        except FileNotFoundError:
            pass
    log(f"  copied intact: {source.name}")
    return [destination]
