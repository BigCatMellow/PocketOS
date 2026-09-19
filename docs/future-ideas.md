# PocketOS Future Ideas

Status: PARKED IDEAS / NOT ACTIVE SCOPE

Purpose: preserve promising future directions without letting them compete with the active `Release Confidence V1` stabilization work.

## Gen1Recomp / LÖVE runtime support

Status: PLANNED / NOT ACTIVE IMPLEMENTATION

The concrete owner for this idea is now:

- [Gen1Recomp on Miyoo Mini Plus roadmap](roadmap-gen1recomp-miyoo.md)

The target is deliberately narrower than generic LÖVE compatibility: run the normal 2D Gen1Recomp experience on the Miyoo Mini Plus with its upstream mod framework available for lightweight QoL/content/balance/UI mods.

The intended architecture remains additive:

```text
PocketOS library entry
→ Miyoo Gen1Recomp launch wrapper
→ ARMv7 LÖVE 11.5 runtime
→ Miyoo SDL2 / EGL/GLES2 / audio stack
→ upstream Gen1Recomp game.love
```

PocketOS remains a native lightweight launcher. LÖVE must remain an optional runtime rather than a dependency of normal PocketOS operation.

### Boundaries

- Do **not** rewrite the PocketOS launcher in LÖVE.
- Do **not** make LÖVE a dependency of normal PocketOS operation.
- Do **not** make voxel/3D rendering, heavy shaders, online play, Gen 2, or arbitrary desktop LÖVE compatibility part of the first target.
- Prove the LÖVE runtime and Gen1Recomp directly on real Miyoo hardware before changing PocketOS integration.
- Do **not** promote implementation ahead of `docs/roadmap-release-confidence-v1.md` without an explicit human project-priority decision.

### Promotion condition

Implementation becomes active only after an explicit project-priority decision. The roadmap's first execution wave begins with ARMv7 LÖVE feasibility and a minimal real-device smoke test, not PocketOS core changes.
