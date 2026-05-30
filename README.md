# PocketOS

A minimal vertical-list launcher for the [Miyoo Mini Plus](https://lomiyoo.com/), built on top of [Onion OS](https://github.com/OnionUI/Onion). Replaces the default grid menu with a clean list interface — everything else stays the same.

Requires Onion OS to be installed first. If you haven't set that up yet, grab it [here](https://github.com/OnionUI/Onion/releases/latest). The boot screen flasher is included on your SD card under Apps once Onion is installed.

---

## Screenshots

<p align="center">
  <img src="screenshots/01 - bootscreen.png" width="480" alt="Boot Screen">
</p>

<p align="center">
  <img src="screenshots/02 - splash.png" width="480" alt="Splash Screen">
</p>

**Home Screen**
<p align="center">
  <img src="screenshots/03 - Home Screen.png" width="480" alt="Home Screen">
</p>

**Favorites**
<p align="center">
  <img src="screenshots/04 - Favorites.png" width="480" alt="Favorites">
</p>

**Library**
<p align="center">
  <img src="screenshots/05 - Library.png" width="480" alt="Library">
</p>

**Browse by Genre**
<p align="center">
  <img src="screenshots/06 - Browse.png" width="480" alt="Browse by Genre">
</p>

**Apps**
<p align="center">
  <img src="screenshots/07 - Apps.png" width="480" alt="Apps">
</p>

**Settings**
<p align="center">
  <img src="screenshots/08 - Settings.png" width="480" alt="Settings">
  <img src="screenshots/09 - Settings 2.png" width="480" alt="Settings 2">
</p>

---

## Install

### PocketOS Setup Suite (recommended)

The installer is a full setup tool — it handles everything in one go:

1. **Installs PocketOS** onto your SD card
2. **Imports ROMs** from a folder you choose — unzips them and places each game in the correct system folder automatically
3. **Cleans up bad dumps** — optionally removes inferior variants (bad dumps, overdumps, hacks, pirates) and keeps the best verified dump per game
4. **Scans genres** using the OpenVGDB database so Browse by Genre works out of the box
5. **Applies genre overrides** to fix common mis-tags

Download the installer for your platform:

| Platform | Download |
|----------|---------|
| Linux | `PocketOS-Installer-linux.tar.gz` — extract, then run `./PocketOS-Installer-linux` |
| Windows | `PocketOS-Installer-windows.exe` |
| macOS | `PocketOS-Installer-macos.tar.gz` — extract, then run `./PocketOS-Installer-macos` |

Point it at your SD card, optionally point it at a folder of ROM ZIPs, and click **Set Up PocketOS**. The installer also checks for newer versions automatically.

### Manual install

Download `pocketOS-vX.X.zip` and extract it to the **root of your SD card**. The `.tmp_update` folder will merge with the existing one. Eject and boot.

### Uninstall

Delete `.tmp_update/bin/pocketOS` from your SD card and reboot. The default Onion menu comes back.

---

## Browse by Genre

PocketOS can filter your library by genre. The Setup Suite handles this automatically during install. If you want to re-scan genres separately, use the standalone **Genre Scanner**:

| Platform | Download |
|----------|---------|
| Linux | `PocketOS-GenreScanner-linux.tar.gz` — extract, then run `./PocketOS-GenreScanner-linux` |
| Windows | `PocketOS-GenreScanner-windows.exe` |
| macOS | `PocketOS-GenreScanner-macos.tar.gz` — extract, then run `./PocketOS-GenreScanner-macos` |

Point it at your SD card and it'll scan all your ROM folders and write `miyoogamelist.xml` files for each system.

---

## ROM Import

The Setup Suite includes a standalone **ROM Importer** tool if you want to add games after initial setup:

| Platform | Download |
|----------|---------|
| Linux | `PocketOS-ROMImporter-linux.tar.gz` — extract, then run `./PocketOS-ROMImporter-linux` |
| Windows | `PocketOS-ROMImporter-windows.exe` |
| macOS | `PocketOS-ROMImporter-macos.tar.gz` — extract, then run `./PocketOS-ROMImporter-macos` |

It scans a folder for ZIP files, extracts the ROMs into the correct system folders on your SD card, optionally removes bad/duplicate dumps, and re-scans genres — the same pipeline the installer runs.

---

## Credits

Built on [Onion OS](https://github.com/OnionUI/Onion). Icons from the Onion default icon set.
