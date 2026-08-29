# Handheld UI Redesign

PocketOS 1.2.1 implements the latest five-row direction from the local design
prototype at `/home/home/Projects/Handheld game UI design/Handheld UI.dc.html`.
The prototype SHA-256 at implementation time was
`4ac07b3312846b29092ee9b5a62b46eb96a4431226b68b51da22cc803e78b986`.

## Primary Navigation

The top bar contains five peer categories:

1. Most Played
2. Browse
3. Library
4. Favorites
5. Settings

`L1` and `R1` move between categories. `L2` and `R2` page within long lists,
so paging cannot accidentally leave the current category. `MENU` opens Settings
from any game category. Apps, full device settings, device information, PocketOS
information, and Sleep live in the Settings category.

## Rendering Contract

The primary shell targets the Miyoo Mini Plus at 640x480. It uses flat fills,
five visible game rows, geometric glyphs, text system badges, and no thumbnails,
blur, alpha animation, or network-loaded assets. The primary palette is fixed for
legibility; user themes continue to apply to the deeper Apps and Settings views.

The source prototype's three accents remain: amber for Most Played, mint for
Browse, and violet for Library. Favorites adds coral and Settings adds blue so
all five categories remain easy to identify without turning the interface into
a single-hue theme.

Run `python3 tools/render_handheld_ui.py` to compile the host launcher, generate
a representative Onion SD fixture, render all five primary views, and reject
blank or duplicate screenshots.
