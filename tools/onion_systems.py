"""Canonical Onion ROM-folder knowledge used by PocketOS desktop tools.

Folder names here follow Onion's documented ROM-folder contract.  Importers may
recognize a few optional/expert folders when they already exist, but they must
not create those folders as if they were core Onion systems.
"""

from __future__ import annotations

# Core Onion ROM folders used by PocketOS tooling.  This is the single owner for
# desktop-side folder naming and OpenVGDB labels.
SYSTEMS = {
    "FC": {
        "display": "Nintendo Entertainment System",
        "openvgdb": "Nintendo Entertainment System",
        "extensions": (".nes",),
    },
    "FDS": {
        "display": "Famicom Disk System",
        "openvgdb": None,
        "extensions": (".fds",),
    },
    "SFC": {
        "display": "Super Nintendo",
        "openvgdb": "Nintendo Super Nintendo Entertainment System",
        "extensions": (".sfc", ".smc"),
    },
    "GB": {
        "display": "Game Boy",
        "openvgdb": "Nintendo Game Boy",
        "extensions": (".gb",),
    },
    "GBC": {
        "display": "Game Boy Color",
        "openvgdb": "Nintendo Game Boy Color",
        "extensions": (".gbc",),
    },
    "GBA": {
        "display": "Game Boy Advance",
        "openvgdb": "Nintendo Game Boy Advance",
        "extensions": (".gba",),
    },
    "NDS": {
        "display": "Nintendo DS",
        "openvgdb": "Nintendo DS",
        "extensions": (".nds",),
    },
    "VB": {
        "display": "Virtual Boy",
        "openvgdb": "Nintendo Virtual Boy",
        "extensions": (".vb",),
    },
    "MD": {
        "display": "Genesis / Mega Drive",
        "openvgdb": "Sega Genesis/Mega Drive",
        "extensions": (".md", ".smd", ".gen"),
    },
    "MS": {
        "display": "Master System",
        "openvgdb": "Sega Master System",
        "extensions": (".sms",),
    },
    "GG": {
        "display": "Game Gear",
        "openvgdb": "Sega Game Gear",
        "extensions": (".gg",),
    },
    "PCE": {
        "display": "PC Engine / TurboGrafx-16",
        "openvgdb": "NEC PC Engine/TurboGrafx-16",
        "extensions": (".pce",),
    },
    "LYNX": {
        "display": "Atari Lynx",
        "openvgdb": "Atari Lynx",
        "extensions": (".lnx",),
    },
    "WS": {
        "display": "WonderSwan / Color",
        "openvgdb": "Bandai WonderSwan",
        "extensions": (".ws", ".wsc"),
    },
    "NGP": {
        "display": "Neo Geo Pocket / Color",
        "openvgdb": "SNK Neo Geo Pocket",
        "extensions": (".ngp", ".ngc", ".ngpc"),
    },
    "COLECO": {
        "display": "ColecoVision",
        "openvgdb": "Coleco ColecoVision",
        "extensions": (".col",),
    },
    "PS": {
        "display": "PlayStation",
        "openvgdb": "Sony PlayStation",
        "extensions": (".pbp",),
    },
    # These are valid Onion folders but archive/disc-image extensions cannot
    # identify them safely by extension alone.
    "ARCADE": {"display": "Arcade", "openvgdb": None, "extensions": ()},
    "NEOGEO": {"display": "Neo Geo", "openvgdb": None, "extensions": ()},
    "NEOCD": {"display": "Neo Geo CD", "openvgdb": None, "extensions": ()},
    "PCECD": {"display": "PC Engine CD", "openvgdb": "NEC PC Engine CD/TurboGrafx-CD", "extensions": ()},
    "SEGACD": {"display": "Sega CD", "openvgdb": "Sega CD/Mega-CD", "extensions": ()},
    "THIRTYTWOX": {"display": "Sega 32X", "openvgdb": "Sega 32X", "extensions": ()},
    "ATARI": {"display": "Atari 2600", "openvgdb": "Atari 2600", "extensions": ()},
    "SGFX": {"display": "SuperGrafx", "openvgdb": "NEC SuperGrafx", "extensions": ()},
    "VECTREX": {"display": "Vectrex", "openvgdb": "GCE Vectrex", "extensions": ()},
}

# Old PocketOS names may exist on a user's custom card.  They are accepted for
# scanning/display compatibility but are never selected as new import targets.
LEGACY_FOLDER_ALIASES = {
    "NES": "FC",
    "SNES": "SFC",
    "SGB": "GB",
    "GEN": "MD",
    "GENESIS": "MD",
    "SMS": "MS",
    "VBOY": "VB",
    "SCD": "SEGACD",
    "32X": "THIRTYTWOX",
    "2600": "ATARI",
    "ATARI2600": "ATARI",
    "WSWAN": "WS",
    "WSWANC": "WS",
    "NGPC": "NGP",
    "PS1": "PS",
    "PSX": "PS",
}

# Unique cartridge/executable formats that are safe to auto-route.
AUTO_SYSTEM_BY_EXT = {
    ext: folder
    for folder, info in SYSTEMS.items()
    for ext in info["extensions"]
}

# These formats are used by several systems (and ZIP is often the ROM itself for
# arcade/Neo Geo).  They must never silently imply PlayStation or any other
# target system.
AMBIGUOUS_EXTENSIONS = frozenset({
    ".bin", ".cue", ".iso", ".img", ".chd", ".zip", ".7z", ".rom"
})

# Optional/expert formats can be used only when the corresponding folder is
# already present on the card.  PocketOS must not create these folders itself.
EXISTING_FOLDER_ONLY_BY_EXT = {
    ".n64": "N64",
    ".z64": "N64",
    ".v64": "N64",
}

# These formats acquire meaning only after the user explicitly selects an
# installed Onion folder.  ZIP remains intact for arcade hardware, where it is
# commonly the ROM rather than a container to unpack.
EXPLICIT_IMPORT_EXTENSIONS = {
    "PS": frozenset({".cue", ".bin", ".iso", ".img", ".chd", ".m3u"}),
    "PCECD": frozenset({".cue", ".bin", ".iso", ".img", ".chd", ".m3u"}),
    "SEGACD": frozenset({".cue", ".bin", ".iso", ".img", ".chd", ".m3u"}),
    "NEOCD": frozenset({".cue", ".bin", ".iso", ".img", ".chd", ".m3u"}),
    "ARCADE": frozenset({".zip"}),
    "NEOGEO": frozenset({".zip"}),
}

ROM_EXTENSIONS = frozenset(
    set(AUTO_SYSTEM_BY_EXT) | set(AMBIGUOUS_EXTENSIONS) | set(EXISTING_FOLDER_ONLY_BY_EXT)
)


def canonical_folder(folder: str) -> str:
    """Return the canonical Onion folder for a known legacy alias."""
    upper = folder.upper()
    return LEGACY_FOLDER_ALIASES.get(upper, upper)


def candidates_for_extension(ext: str) -> tuple[str, ...]:
    """Safe candidate folders for extension-only import classification."""
    ext = ext.lower()
    if ext in AMBIGUOUS_EXTENSIONS:
        return ()
    folder = AUTO_SYSTEM_BY_EXT.get(ext) or EXISTING_FOLDER_ONLY_BY_EXT.get(ext)
    return (folder,) if folder else ()


def extensions_for_folder(folder: str) -> frozenset[str]:
    """ROM extensions valid for an explicit/installed target folder."""
    canonical = canonical_folder(folder)
    info = SYSTEMS.get(canonical)
    if info:
        return frozenset(info["extensions"]) | EXPLICIT_IMPORT_EXTENSIONS.get(canonical, frozenset())
    if canonical == "N64":
        return frozenset({".n64", ".z64", ".v64"})
    return frozenset()


def openvgdb_system_name(folder: str, rom_suffix: str | None = None) -> str | None:
    """Return OpenVGDB system name, retaining safe legacy scan compatibility."""
    canonical = canonical_folder(folder)
    info = SYSTEMS.get(canonical)
    if not info:
        # Optional/expert systems retain existing known mappings without becoming
        # auto-created Onion targets.
        if canonical == "N64":
            return "Nintendo 64"
        return None
    if canonical == "NGP" and (rom_suffix or "").lower() in {".ngc", ".ngpc"}:
        return "SNK Neo Geo Pocket Color"
    return info["openvgdb"]


def display_name(folder: str) -> str:
    canonical = canonical_folder(folder)
    info = SYSTEMS.get(canonical)
    return info["display"] if info else folder
