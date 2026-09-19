# Gen1Recomp on Miyoo Mini Plus

Status: PLANNED / NOT ACTIVE IMPLEMENTATION

Owner: PocketOS project

Method: MAPS_L Project Bootstrap

Primary target: Miyoo Mini Plus running Onion OS + PocketOS

Upstream application: [bryanthaboi/gen1recomp](https://github.com/bryanthaboi/gen1recomp)

## Parent outcome

Run the normal 2D Gen1Recomp experience on a Miyoo Mini Plus from PocketOS, using a legally supplied Pokemon Red ROM, with the upstream mod framework usable for lightweight quality-of-life/content mods.

The result should feel like a native handheld game: launch from PocketOS, use the Miyoo controls, retain saves/cache/mod configuration on the SD card, run the upstream LOW-performance presentation path, and return cleanly to Onion/PocketOS.

This project is **not** an attempt to bring Gen1Recomp's voxel/3D showcase features to the Miyoo.

## Current authority and priority

This roadmap records the future project; it does not activate implementation.

PocketOS `Release Confidence V1` remains the active higher-priority roadmap. The Gen1Recomp project may be promoted by explicit human priority decision. Planning and bounded non-invasive feasibility research may be done without changing PocketOS runtime behavior.

No public PocketOS release artifact may include this work until the release-confidence publication gates are satisfied.

## Reality snapshot

Checked 2026-09-19.

### VERIFIED

- PocketOS `main` snapshot inspected: `4698d14386b2af35332789383f37907ba7e012ee`.
- PocketOS is a native C/SDL launcher on Onion OS and already hands game launching to Onion-style launch scripts.
- PocketOS currently parks LÖVE support in `docs/future-ideas.md`.
- Gen1Recomp upstream `dev` snapshot inspected: `65a1fcda25b450025856516862f8b245655455ae`.
- Gen1Recomp requires LÖVE 11.x and currently targets LÖVE 11.5.
- Gen1Recomp has controller support, direct game launch options, portable storage behavior, a mod loader/manager, and an automatic LOW-performance tier for ARM handhelds.
- Upstream already ships a PortMaster-style RG34XXSP handheld package that bundles a LÖVE 11.5 aarch64 runtime.
- The RG34XXSP package is not directly reusable on the Miyoo Mini Plus because it is a 64-bit ARM target.
- A current Miyoo Mini Plus SDL2 port exists at `Rparadise-Team/sdl2_miyoo_new`, supports Onion v4.3.1-1, and provides an SDL2 path plus EGL/GLESv2 libraries for the device.
- Gen1Recomp's frame-cap floor is 30 FPS and its default is 60 FPS.

### UNKNOWN / must be proved on hardware

- Whether LÖVE 11.5 can be built into a reliable 32-bit ARMv7 Miyoo runtime with the required modules.
- Whether the resulting graphics path is fast enough for Gen1Recomp's ordinary 2D presentation.
- Whether audio works reliably with Onion's device stack.
- Peak/runtime memory behavior on the Miyoo Mini Plus.
- Exact Miyoo gamepad mapping as exposed through SDL2/LÖVE.
- Any upstream code assumptions that break under the Miyoo's older glibc/device environment.
- Whether the upstream mod manager UI is comfortable at 640x480 without adaptation.

## Scope

### Required

- Pokemon Red as the first supported game.
- Normal 2D presentation.
- Gen1Recomp's upstream game logic.
- D-pad, A, B, Start, Select.
- Save/load and persistent options.
- ROM import from a user-supplied legal canonical ROM.
- ROM-derived cache stored privately on the SD card.
- Upstream mod loader.
- Upstream mod manager or an equivalent handheld-accessible mod-management route.
- Lightweight QoL/content/balance/UI mods that use the normal mod API.
- Clean launch from and return to Onion/PocketOS.
- Reproducible build/package instructions.

### Optional after the required target works

- Blue and Yellow.
- L/R bindings for useful shortcuts.
- Direct PocketOS entries per imported game.
- Generic `.love` application support when it falls out naturally from the runtime.

### Explicit non-goals

- Voxel / Dramatic Shape rendering.
- 3D tilt/survey presentation as an acceptance requirement.
- Heavy shader compatibility.
- Desktop save editor on the handheld.
- Online play.
- Gen 2 support.
- Arbitrary desktop LÖVE compatibility.
- Rewriting PocketOS in LÖVE.
- Making LÖVE a dependency of normal PocketOS operation.

## Intended architecture

```text
PocketOS library entry
        |
        v
Miyoo Gen1Recomp launch wrapper
        |
        +--> user ROM / generated cache / saves / mods
        |
        v
ARMv7 LÖVE 11.5 runtime
        |
        v
Miyoo SDL2 + EGL/GLES2 + audio stack
        |
        v
upstream Gen1Recomp game.love
```

Keep the runtime isolated. PocketOS should treat Gen1Recomp as another launchable system/application rather than absorbing LÖVE into the launcher process.

## Definition of DONE

The parent project is DONE only when all of these are demonstrated on a real Miyoo Mini Plus:

- [ ] PocketOS launches Gen1Recomp without destabilizing normal PocketOS/Onion operation.
- [ ] A canonical user-supplied Pokemon Red ROM imports successfully.
- [ ] Subsequent launches reuse the generated cache and do not require re-import.
- [ ] D-pad, A, B, Start, and Select work consistently.
- [ ] Gameplay reaches and can progress through ordinary overworld, menu, and battle flows.
- [ ] Audio/music function without persistent corruption, stalls, or device-loss failures.
- [ ] Save, quit, relaunch, and load succeed.
- [ ] At least three representative lightweight mods work through the normal mod API: one QoL/UI/tool mod, one data/balance mod, and one gameplay/content mod.
- [ ] The user can enable/disable those mods from a handheld-usable path.
- [ ] A sustained real-device play session completes without crash, runaway memory growth, or unusable input/audio timing.
- [ ] Observed FPS/memory are recorded; 30 FPS is the minimum acceptable presentation floor for the ordinary 2D path.
- [ ] Exit returns cleanly to Onion/PocketOS.
- [ ] Build and SD-card packaging are reproducible from documented source inputs.
- [ ] Integration receives independent review before inclusion in a PocketOS release candidate.

## Roadmap

### Phase 0 — runtime feasibility

Goal: answer the hardest question before changing PocketOS.

1. Inventory LÖVE 11.5 build dependencies against the Miyoo ARMv7 toolchain.
2. Compare upstream Gen1Recomp's aarch64/PortMaster build decisions with the Miyoo SDL2 stack.
3. Build the smallest possible ARMv7 LÖVE 11.5 runtime.
4. Run a minimal LÖVE program on real Miyoo hardware.
5. Verify screen, buttons, audio, filesystem writes, clean exit, FPS and RSS.

**Gate:** if basic LÖVE cannot run reliably, stop PocketOS integration work and either repair the runtime path or close/cut the project.

### Phase 1 — Gen1Recomp direct boot

Goal: prove the application outside PocketOS first.

1. Package the upstream `game.love` with the Miyoo runtime.
2. Force LOW-performance presentation and disable/deprioritize expensive optional effects.
3. Start with a direct Red launch/import route rather than desktop file-picker behavior.
4. Put cache, save, options and mods in a deliberate SD-card-owned location.
5. Verify controller mapping, audio, basic play, save/load and exit.

**Gate:** ordinary Red gameplay must be usable before work continues to mod UX or PocketOS integration.

### Phase 2 — mod/QoL target

Goal: prove the feature the project exists for.

Test representative mods through the real upstream mod loader:

- QoL/UI/tool category;
- data/balance category;
- gameplay/content category.

Prefer small known-good mods first. Do not use voxel/3D mods as a compatibility test.

If F10/desktop navigation is awkward, add the smallest handheld-specific access path, preferably through the normal Options/Start UI or a bounded button chord.

**Gate:** mods must enable/disable predictably without corrupting saves or requiring desktop-only input.

### Phase 3 — handheld hardening

Goal: turn a successful demo into a reliable port.

- Measure FPS, RSS/available memory and startup/import time.
- Exercise battle, overworld, menus, repeated save/load, repeated launch/exit, and mod toggling.
- Verify low-memory and audio-reset behavior where reproducible.
- Remove or gate unnecessary desktop-only workers/features if they materially cost memory or stability.
- Retain upstream behavior wherever adaptation is unnecessary.

**Gate:** sustained device evidence supports normal play at the defined acceptance floor.

### Phase 4 — PocketOS integration

Goal: make the proven standalone port discoverable and launchable without coupling PocketOS to LÖVE.

- Add an Onion/PocketOS-compatible system/app definition.
- Add launch wrapper and argument/path handling.
- Ensure ROM/cache/save/mod paths are quoted and bounded.
- Preserve Onion fallback and normal PocketOS launch behavior.
- Add focused host tests for the new launch contract.
- Verify launch and return on device.

**Gate:** Gen1Recomp is additive; removing its package leaves normal PocketOS behavior unchanged.

### Phase 5 — reproducible package and review

Goal: make the result independently reproducible.

- Pin runtime/build inputs.
- Document the build and SD layout.
- Package no Pokemon ROM or ROM-derived cache.
- Record exact PocketOS and Gen1Recomp revisions used for validation.
- Retain device evidence.
- Obtain independent implementation review.
- Only after PocketOS release-confidence gates permit publication, evaluate inclusion in a release candidate.

## First execution wave after promotion

Do not begin with PocketOS code changes.

### Task A — ARMv7 LÖVE dependency matrix

Output: one build matrix mapping each required LÖVE 11.5 dependency to Miyoo/Onion availability, bundled build, or unresolved gap.

Stop condition: any mandatory dependency has no plausible ARMv7 path.

### Task B — minimal LÖVE hardware smoke test

Output: runtime package + tiny program proving render/input/audio/write/exit on the device.

Evidence: build log, exact hashes/revisions, device log, FPS and memory sample.

Stop condition: repeated instability or performance failure that cannot be isolated.

### Task C — Gen1Recomp no-mod smoke test

Output: Red imports and reaches playable gameplay on hardware.

Evidence: launch log, input/audio/save/load checks, performance sample.

Dependency: A + B pass.

Only after Task C passes should mod compatibility or PocketOS integration become eligible.

## Risks and challenge

| Risk | Consequence | Response |
| --- | --- | --- |
| ARMv7 LÖVE 11.5 dependency failure | project blocked before game boot | reuse upstream build knowledge but maintain a Miyoo-specific dependency matrix; stop rather than patch blindly |
| software/GLES rendering too slow | technically boots but poor game | force LOW tier, keep 2D target, measure early; cut visual extras before changing game logic |
| 128 MB-class device memory pressure | crashes/stalls | measure RSS from the first smoke test; remove optional desktop services only when evidence identifies them |
| audio backend incompatibility | unplayable experience | test audio in the minimal runtime before Gen1Recomp |
| mod UI assumes desktop inputs | mods exist but are hard to control | preserve loader; adapt only the access/navigation surface |
| upstream moves quickly | port patches rot | minimize fork delta; pin a known-good upstream revision for each device validation |
| scope drifts into generic LÖVE platform | delays actual goal | Red + QoL mods remains parent outcome; generic LÖVE support is optional follow-on only |
| PocketOS stabilization gets displaced | release-confidence work regresses | roadmap stays non-active until explicit priority promotion |

## Verification and review

Risk: MEDIUM during planning/build research; HIGH once device integration/package changes can affect PocketOS/Onion behavior.

- Runtime feasibility: direct real-device reproduction.
- Gen1Recomp compatibility: real-device play evidence.
- Mod compatibility: representative real mods through the production loader.
- PocketOS integration: focused automated launch-contract tests + device reproduction.
- Final integration: independent review bound to exact revisions plus operator-visible completion summary.

## Change / stop rules

Use evidence to choose:

- **CONTINUE** when the current gate passes.
- **RESEARCH** when a specific dependency/API/device behavior is unknown.
- **CHANGE** when a bounded port adaptation is required.
- **CUT SCOPE** when an optional visual/desktop feature threatens the parent outcome.
- **STOP** if the runtime cannot meet the basic 2D gameplay floor on the hardware without effectively replacing LÖVE or Gen1Recomp.

Do not turn a failed generic LÖVE feature (for example shaders) into a blocker unless Gen1Recomp's required 2D/QoL path actually needs it.
