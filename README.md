# PocketOS v1.0

A minimal launcher skin built on top of Onion OS for the Miyoo Mini Plus. Replaces the default grid-based main menu with a clean vertical list interface, while keeping all of Onion's features intact underneath.

---

## Requirements

- **Miyoo Mini Plus** (not compatible with the original Miyoo Mini)
- **Onion OS** installed on your SD card (v4.2 or later recommended)

---

## Install

1. Download `pocketOS-v1.0.zip`
2. Extract it to the **root of your SD card** — the `.tmp_update` folder will merge with the existing one
3. Safely eject your SD card and insert it into your Miyoo Mini Plus
4. Power on — PocketOS will launch automatically

That's it. Onion OS detects the binary and uses it instead of the default main menu.

---

## Uninstall

Delete `.tmp_update/bin/pocketOS` from your SD card and reboot. The default Onion OS menu will return.

---

## What's included

- **Vertical list navigation** — Home, Library, Browse by genre, Recents, Favourites, Apps, Settings
- **37 themes** — selectable from Settings → Theme (Lavender, Nord, Dracula, Catppuccin, Tokyo Night, and many more)
- **Multiple fonts** — selectable from Settings → Font
- **Full settings panel** — brightness, volume, Wi-Fi, blue light filter, sleep timer, and more
- **Complete app launcher** — all your installed Onion apps in one place
- **Browse by genre** — explore your library filtered by genre (requires `miyoogamelist.xml` files)
- **Screenshots** — hold L1 + L2 + R1 + R2 to capture the UI, saved to `/Screenshots/`
- **Device & About info panels** — firmware, Onion OS version, kernel info

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
