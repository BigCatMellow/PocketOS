# PocketOS

A focused game launcher for the [Miyoo Mini Plus](https://lomiyoo.com/), built on top of [Onion OS](https://github.com/OnionUI/Onion). The five-category interface keeps Most Played, Browse, Library, Favorites, and Settings one shoulder press apart while retaining Onion's emulators, apps, and runtime.

PocketOS 1.2.8 implements the latest low-overhead design in [the handheld UI notes](docs/handheld-ui-redesign.md): five visible rows, flat high-contrast colors, real library counts, and no thumbnails or GPU-heavy effects.

Requires Onion OS to be installed first. If you haven't set that up yet, grab it [here](https://github.com/OnionUI/Onion/releases/latest). The boot screen flasher is included on your SD card under Apps once Onion is installed.

---

## Screenshots

**Boot Screen**
<p align="center">
  <img src="screenshots/bootscreen.png" width="480" alt="PocketOS boot screen">
</p>

**Splash Screens**
<p align="center">
  <img src="screenshots/splashscreen.png" width="480" alt="PocketOS splash screen">
  <img src="screenshots/splashscreen2.png" width="480" alt="PocketOS alternate splash screen">
</p>

**Most Played**
<p align="center">
  <img src="screenshots/1_Most_Played.png" width="480" alt="Most Played">
</p>

**Browse**
<p align="center">
  <img src="screenshots/2_Browse.png" width="480" alt="Browse">
</p>

**Library**
<p align="center">
  <img src="screenshots/3_Library.png" width="480" alt="Library">
</p>

**Favorites**
<p align="center">
  <img src="screenshots/4_Favorties.png" width="480" alt="Favorites">
</p>

**Settings**
<p align="center">
  <img src="screenshots/5_Settings.png" width="480" alt="Settings">
</p>

---

## Fonts

Use **Settings → Appearance → Font** to choose a typeface. PocketOS includes compact, high-legibility options for the Miyoo Mini Plus display: DejaVu Sans, Lato, Noto Sans, Roboto Condensed, and Kenney Future Narrow.

---

## Themes

PocketOS includes 53 built-in color schemes. In **Settings → Appearance → Theme**, choose from the original light palettes and purpose-built dark variants including Ayu, Catppuccin Mocha, Dracula, Everforest, Gruvbox, Kanagawa, Monokai, Nord, One Dark, Rosé Pine, Solarized, and Tokyo Night. Changes preview live before you confirm them.

---

## Health monitoring (optional)

PocketOS can write a small local health log—memory use, available memory, battery level, brightness, and screen state—to help diagnose long-session issues. It records no game names and sends nothing online. Create an empty file named `.tmp_update/logs/pocketos_health.csv` on the SD card to enable it. After using PocketOS, run `python3 pocketos-health-report.py /path/to/your/SD-card` on your computer. For an unattended PocketOS-only regression test, run `sh /mnt/SDCARD/pocketos-stress-test.sh 30` from Onion Terminal; it runs for 30 minutes, cycles PocketOS views, and stops automatically. For a fair PocketOS-versus-Onion comparison, run the same 30-minute manual routine once per launcher: `sh /mnt/SDCARD/launcher-comparison-monitor.sh start pocketos 30`, then later `sh /mnt/SDCARD/launcher-comparison-monitor.sh start onion 30`. Run `python3 pocketos-health-report.py --compare /path/to/your/SD-card` afterward. The monitor records only launcher memory, available memory, battery, and CPU governor.

---

## Install

### PocketOS Setup Suite (recommended)

The installer is a terminal setup tool that validates your SD card before it copies anything. It handles everything in one go:

1. **Installs PocketOS** onto your SD card
2. **Imports ROMs** from a folder you choose — unzips them and places each game in the correct system folder automatically
3. **Cleans up bad dumps** — optionally removes inferior variants (bad dumps, overdumps, hacks, pirates) and keeps the best verified dump per game
4. **Scans genres** using the OpenVGDB database so Browse by Genre works out of the box
5. **Applies genre overrides** to fix common mis-tags

Download the installer for your platform:

| Platform | Download |
|----------|---------|
| Linux | `PocketOS-Installer-linux.tar.gz` — extract, then run `./PocketOS-Installer-linux` |
| Windows | `PocketOS-Installer-windows.zip` — extract, then run `PocketOS-Installer-windows.exe` |
| macOS | `PocketOS-Installer-macos.tar.gz` — extract, then run `./PocketOS-Installer-macos` |

Point it at your SD card and choose **Install PocketOS**. The installer checks for Onion's runtime files, installs a fail-open launcher hook, makes the PocketOS binary executable, can import ROM ZIPs, and checks for newer versions automatically.

### Manual install

Download `pocketOS-vX.X.zip` and extract it to the **root of your SD card**. Then open a terminal in the SD card root and run `python3 install-pocketos.py`. This installs a fail-open Onion launcher hook; if PocketOS ever fails, the stock Onion menu is used for the rest of that boot.

### Uninstall

Use the installer's **Uninstall PocketOS** option to remove PocketOS and restore the stock Onion launch block. Deleting `.tmp_update/bin/pocketOS` is also safe: the launcher hook will use Onion's default menu when the PocketOS binary is absent.

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
