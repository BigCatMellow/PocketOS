# PocketOS Future Ideas

Status: PARKED IDEAS / NOT ACTIVE SCOPE

Purpose: preserve promising future directions without letting them compete with the active `Release Confidence V1` stabilization work.

## LÖVE / Love2D runtime support

Status: PARKED EXPERIMENT

### Idea

Explore whether PocketOS can run existing programs and games built with **LÖVE (Love2D)** on the Miyoo Mini Plus.

The intended architecture is **not** to rewrite PocketOS in LÖVE. PocketOS would remain a native lightweight launcher and optionally launch `.love` packages through a Miyoo-compatible LÖVE runtime.

Conceptual flow:

```text
PocketOS library entry
→ PocketOS LÖVE launcher wrapper
→ Miyoo-compatible LÖVE runtime
→ SDL2 / GLES / audio stack
→ existing .love game or program
```

Possible library layout:

```text
Roms/LOVE/
├── Game1.love
├── Game2.love
└── SomeProgram/
    ├── main.lua
    ├── conf.lua
    └── assets/
```

### Why it may be feasible

- LÖVE projects are primarily Lua content executed by a runtime, so individual programs would not necessarily require recompilation.
- Existing Miyoo Mini Plus community work provides SDL2/GLES-capable foundations that may make an ARM LÖVE port practical.
- PocketOS already has a native launch boundary that could potentially treat LÖVE as another runtime/system rather than embedding it into the core launcher.

### Main unknowns

- whether LÖVE 11.x can be compiled and run reliably on the Miyoo Mini Plus environment;
- graphics performance, especially where GLES is software-rendered;
- memory overhead on the Miyoo Mini Plus;
- audio-library compatibility;
- Miyoo control mapping and keyboard/mouse assumptions in existing LÖVE projects;
- save/config paths and clean return to PocketOS/Onion;
- compatibility limits for shaders, canvases, particles, large asset sets, and high draw-call workloads.

### First proof of concept

When this idea is promoted, keep the first experiment deliberately small:

1. build a Miyoo-compatible LÖVE 11.x runtime;
2. launch a minimal `main.lua` on real hardware;
3. verify D-pad/buttons, sprite rendering, text, sound, music, save/load, exit/return, FPS, and memory use;
4. progressively test sprites, scaling, audio, many sprites, canvases, particles, then shaders;
5. try one small existing `.love` game without modifying PocketOS core behavior;
6. only if runtime/device results are acceptable, prototype PocketOS library discovery and launching for `.love` files.

### Boundaries

- Do **not** rewrite the PocketOS launcher in LÖVE as part of this idea.
- Do **not** make LÖVE a dependency of normal PocketOS operation.
- Treat it as an optional runtime.
- Do **not** promote this into active implementation while `docs/roadmap-release-confidence-v1.md` remains the higher-priority stabilization work unless the human project owner explicitly changes priority.

### Promotion condition

This idea becomes active only after an explicit project-priority decision and a bounded PoC task. Until then it is preserved for future investigation only.
