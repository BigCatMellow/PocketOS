# PocketOS

A minimal launcher for the Miyoo Mini Plus built on top of Onion OS. Replaces the default grid menu with a clean vertical list interface — all of Onion's features stay intact underneath.

---

## Screenshots

**Boot Screen** — clean minimal boot logo on startup

<p align="center">
  <img src="screenshots/01 - bootscreen.png" width="480" alt="Boot Screen">
</p>

**Splash** — brief splash screen before the menu loads

<p align="center">
  <img src="screenshots/02 - splash.png" width="480" alt="Splash Screen">
</p>

**Home Screen** — vertical list navigation with quick access to every section

<p align="center">
  <img src="screenshots/03 - Home Screen.png" width="480" alt="Home Screen">
</p>

**Favorites** — your pinned games in one place

<p align="center">
  <img src="screenshots/04 - Favorites.png" width="480" alt="Favorites">
</p>

**Library** — full game library organized by system, with full system names in the left panel

<p align="center">
  <img src="screenshots/05 - Library.png" width="480" alt="Library">
</p>

**Browse by Genre** — filter your entire library by genre across all systems (requires Genre Scanner)

<p align="center">
  <img src="screenshots/06 - Browse.png" width="480" alt="Browse by Genre">
</p>

**Apps** — all your installed Onion apps in one launcher

<p align="center">
  <img src="screenshots/07 - Apps.png" width="480" alt="Apps">
</p>

**Settings** — brightness, volume, theme, font, Wi-Fi, sleep timer, and more

<p align="center">
  <img src="screenshots/08 - Settings.png" width="480" alt="Settings">
  <img src="screenshots/09 - Settings 2.png" width="480" alt="Settings 2">
</p>

---

## Requirements

- **Miyoo Mini Plus** (not compatible with the original Miyoo Mini)
- **Onion OS** installed on your SD card (v4.2 or later recommended)

---

## Install

### Option A — GUI Installer (recommended)
Download the installer for your platform from the [latest release](../../releases/latest), run it, point it at your SD card root, and click **Install PocketOS**.

| Platform | Download |
|----------|---------|
| Linux    | `PocketOS-Installer-linux` |
| Windows  | `PocketOS-Installer-windows.exe` |
| macOS    | `PocketOS-Installer-macos` |

### Option B — Manual (SD card zip)
1. Download `pocketOS-vX.X.zip` from the [latest release](../../releases/latest)
2. Extract it to the **root of your Miyoo SD card** — the `.tmp_update` folder will merge with the existing one
3. Safely eject your SD card and insert it into your Miyoo Mini Plus
4. Power on — PocketOS will launch automatically

---

## Uninstall

Use the **Uninstall** button in the GUI installer, or manually delete `.tmp_update/bin/pocketOS` from your SD card and reboot. The default Onion OS menu will return.

---

## What's included

- **Vertical list navigation** — Home, Library, Browse by genre, Recents, Favourites, Apps, Settings
- **37 themes** — selectable from Settings → Theme (Lavender, Nord, Dracula, Catppuccin, Tokyo Night, and many more)
- **Multiple fonts** — selectable from Settings → Font
- **Full settings panel** — brightness, volume, Wi-Fi, blue light filter, sleep timer, and more
- **Complete app launcher** — all your installed Onion apps in one place
- **Browse by genre** — explore your library filtered by genre (use the Genre Scanner tool to generate `miyoogamelist.xml` files)
- **Screenshots** — hold L1 + L2 + R1 + R2 to capture the screen, saved to `/Screenshots/`
- **Device & About info panels** — firmware, Onion OS version, kernel info

---

## Genre Scanner

The Genre Scanner tool scans your ROM folders against the OpenVGDB database and writes `miyoogamelist.xml` files so Browse by Genre works automatically.

| Platform | Download |
|----------|---------|
| Linux    | `PocketOS-GenreScanner-linux` |
| Windows  | `PocketOS-GenreScanner-windows.exe` |
| macOS    | `PocketOS-GenreScanner-macos` |

---

## Controls

| Button | Action |
|--------|--------|
| D-pad Up/Down | Navigate list |
| A / Right | Select / open |
| B / Left | Back |
| L1 / R1 | Page up / down |
| Menu | Return to home |
| L1+L2+R1+R2 | Take screenshot |

---

## Notes

- All your games, save states, and Onion settings are untouched — PocketOS only adds files, never modifies existing ones
- Wi-Fi, RetroArch settings, and all other Onion apps work exactly as before
- The launcher reads your existing Onion game lists and recents automatically

---

## Credits

Built on top of [Onion OS](https://github.com/OnionUI/Onion) — the excellent open-source firmware for the Miyoo Mini Plus. Icons sourced from the Onion OS default icon set.
