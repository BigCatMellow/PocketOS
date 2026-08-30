# PocketOS Reliability Hardening Roadmap

- State: `DRAFT`
- Baseline: `v1.2.4` / `b795aa420b83f0ae19bef7cd837d3b6ba9cdf362`
- Method: MAPS_L Project Bootstrap — inspect reality → define DONE → plan backward → challenge → execute from evidence
- Default autonomous execution after approval: `YES`

## Current reality

PocketOS v1.2.3 completed its Release workflow successfully: ARM build, 41 Python tests, host UI rendering, Linux/macOS/Windows installer builds, SD-card ZIP assembly, and release creation all passed. `main` has since advanced to v1.2.4 with an Onion baseline monitor; its Release workflow was still running when this roadmap baseline was refreshed.

The current tests are useful for runtime-hook behavior, release/environment contracts, UI structure, and presence of the new stress/baseline tooling. They do not exercise the highest-risk ROM import/cleanup/metadata behavior. `main` is currently unprotected, so successful release evidence is not a required merge gate.

### Confirmed high-risk findings

1. **ROM cleanup can delete valid user files.**
   - Cleanup runs across the entire affected system folder, not only newly imported files.
   - Reproduced examples:
     - `Final Fantasy VII (Disc 1/2/3).chd` groups as one title and keeps only Disc 1.
     - `Metal Gear Solid.bin` + `Metal Gear Solid.cue` groups as one title and keeps only the BIN.
     - A translated ROM can lose to the original-language ROM by score.
   - Current installer makes cleanup opt-out rather than opt-in.

2. **Game launch command construction has a shell-escaping bypass.**
   - PocketOS writes launcher and ROM paths into a shell script using double quotes.
   - Onion later escapes `$`, but a filename containing a backslash immediately before command substitution can survive as `\\$(...)` and execute when the command file runs.
   - This was reproduced against Onion's current `runtime.sh` transformation.

3. **Genre scanning can destroy existing metadata.**
   - Existing `miyoogamelist.xml` entries are reduced to `path`, `name`, and `genre` and then rewritten.
   - Other existing metadata can be lost.
   - Parse failure is treated as an empty list rather than a hard stop, increasing corruption risk.

4. **Favorites rewriting drops Onion fields.**
   - Onion's current `JsonGameEntry` includes `label`, `launch`, `type`, `rompath`, and optional `imgpath`.
   - PocketOS reloads only a subset and rewrites favorites with only `label`, `rompath`, and `launch`, dropping `type`, `imgpath`, and any future unknown fields.

5. **PocketOS health-monitor memory evidence is currently invalid.**
   - `proc_kb_value()` scans proc files using `fscanf("%s %ld")`.
   - `/proc/self/status` begins with non-numeric fields and `/proc/meminfo` contains trailing `kB` tokens; the scanner exits before reliably reaching `VmRSS` / `MemAvailable`.
   - The v1.2.4 Onion baseline monitor uses line-oriented `awk` parsing correctly, but the PocketOS-side sampler still needs the same fix before the two datasets are comparable.

6. **Genre acquisition does not match documented Setup Suite behavior.**
   - README says the packaged installer scans genres out of the box and applies overrides.
   - Release payload does not include `openvgdb.sqlite`.
   - Installer/importer look for a loose `fix_unsorted.py`; that file is not present in the repository.
   - The standalone Genre Scanner points at `openvgdb.sqlite.zip`, while OpenVGDB v29.0 publishes `openvgdb.zip`; the configured download URL returns 404.

7. **ROM-system mapping is not reliably Onion-compatible.**
   - Several folder aliases differ from Onion's canonical folders.
   - `NEOGEO` is incorrectly treated as Neo Geo Pocket in PocketOS mapping while Onion uses `NEOGEO` for Neo Geo and `NGP` for Neo Geo Pocket.
   - Ambiguous disc extensions such as `.bin`, `.iso`, `.img`, and `.chd` cannot safely identify a system by extension alone.
   - ZIP is a ROM container for some Onion systems (arcade/Neo Geo), so unconditionally opening/extracting ZIPs is not a valid general import strategy.

8. **Large-ROM hashing is incomplete.**
   - CRC32 currently reads only the first 64 MiB of a file, so large ROM/disc-image hashes are not true whole-file CRCs.

9. **Installer/importer implementation duplicates important logic.**
   - ROM detection, extraction, genre handling, cleanup, and XML writing exist in multiple tools.
   - Safety fixes can drift unless these paths share one implementation owner.

### Things that are working and should be preserved

- Fail-open Onion runtime integration.
- Runtime marker validation and backup behavior.
- Atomic `runtime.sh` replacement.
- Pinned Miyoo cross-toolchain image digest.
- Non-PIE and glibc compatibility checks.
- Host UI rendering evidence.
- Opt-in bounded health logging concept.
- Active PocketOS stress-test concept.
- Separate stock-Onion baseline monitor concept introduced in v1.2.4.
- Current Onion target is still the latest stable Onion release (`v4.3.1-1`).

## Definition of DONE

PocketOS is considered hardened when all of the following are true:

1. Installing, importing, scanning genres, favoriting, updating, and uninstalling cannot silently delete or strip unrelated user data.
2. A ROM/app path cannot cause shell commands to execute through PocketOS command generation.
3. Import behavior matches current Onion folder/ROM contracts, including arcade ZIPs and multi-file/multi-disc games.
4. Every destructive action is previewable/reversible and is never enabled by default.
5. PocketOS and stock-Onion health evidence report valid numeric memory measurements on a real Miyoo Mini Plus.
6. High-risk importer/metadata/launch behavior has executable regression tests, not source-string assertions alone.
7. Pull requests and `main` changes must pass CI before release eligibility.
8. A clean install, update, uninstall, representative ROM import, genre scan, favorites mutation, launch matrix, and long-session device comparison all pass on a sacrificial/test SD card.

### Final proof

A release candidate must pass:

- full CI on the exact release commit;
- independent review of security/data-loss/release paths;
- disposable-SD install → use → update → uninstall round trip;
- importer fixture suite covering cartridge, arcade ZIP, CUE/BIN, CHD, multi-disc, collision, hack/translation, and ambiguous-format cases;
- metadata-preservation fixtures for `miyoogamelist.xml` and `favourite.json`;
- shell-path adversarial launch matrix against Onion's current runtime contract;
- at least a 2-hour on-device PocketOS active soak with valid RSS / available-memory samples;
- a comparable stock-Onion baseline capture so memory/resource conclusions are relative rather than guessed;
- release artifacts smoke-tested on Linux, macOS, and Windows.

## Execution permission envelope

This roadmap is not yet approved for implementation. If approved, routine code changes, tests, branches, commits, pull requests, CI changes, documentation corrections, and reversible test fixtures required by this roadmap are in scope.

Explicitly excluded unless separately authorized:

- deleting or modifying a user's real ROM library during development;
- destructive testing on a non-test SD card;
- changing PocketOS product scope beyond reliability/safety/maintainability;
- distributing third-party databases or assets unless redistribution terms are verified.

Preauthorized destructive class after roadmap approval: **test fixtures and disposable/sacrificial SD-card contents only**, with known backups or reproducible inputs.

## Backward plan

1. **Immediately before DONE:** exact release commit passes CI, independent review, disposable-SD round trip, importer/metadata/launch fixture suite, and on-device PocketOS-vs-Onion qualification.
2. **Before that:** release process requires the relevant CI gates and all high-risk behaviors have executable regression coverage.
3. **Before that:** importer/metadata/launch/health implementations are safe by construction and share canonical utilities rather than duplicated logic.
4. **Current state:** v1.2.4 adds useful baseline instrumentation, but several high-risk paths remain untested or demonstrably unsafe.

## Challenge result

The first instinct might be to split the large C file first. That is the wrong sequence.

A broad refactor before pinning down launch/data contracts would increase change surface while preserving the underlying risks. The first work must establish safety tests and correct destructive/security behavior. Modularization comes after those contracts are executable.

Likewise, the OpenVGDB database should not simply be bundled to make the README true. First verify redistribution terms. The safer near-term path is a verified, explicit download/cache flow from the official release asset.

The new Onion baseline monitor should also not be treated as proof that PocketOS resource use is acceptable by itself. The PocketOS sampler must first produce valid comparable metrics and both runs need similar duration/workload conditions.

## First wave — stop the dangerous failures

### P0-1 — Disable destructive ROM cleanup

- Remove cleanup from the default install/import path or make it explicit preview-only.
- Do not delete pre-existing library files.
- Add fixture tests for:
  - CUE + BIN;
  - multi-disc CHD;
  - M3U sets;
  - translations/hacks;
  - alternate regions;
  - same-name files with different extensions.
- If cleanup returns later, move candidates to a quarantine folder with a manifest rather than unlinking them.

**Pass:** no fixture loses a required file, and ordinary install/import performs zero ROM deletions.

### P0-2 — Harden Onion launch command generation

- Build an executable compatibility test around the current Onion `cmd_to_run.sh` parsing/execution behavior.
- Test spaces, apostrophes, quotes, backslashes, `$`, `$(...)`, backslash-dollar, semicolons, ampersands, pipes, Unicode, and long paths.
- Prefer a non-shell handoff if Onion permits it; otherwise implement a narrowly proven encoding/rejection policy compatible with Onion's parser.
- Apply the same rule to any other user/device-derived value passed through `system()`.

**Pass:** no adversarial filename can create an observable shell side effect; valid ordinary ROM names still launch.

### P0-3 — Make metadata writes preservation-first

- `miyoogamelist.xml`: edit the existing XML tree in place; only modify owned fields.
- Abort on parse failure; never replace unreadable source with a reduced file.
- Preserve unknown XML elements/attributes.
- `favourite.json`: preserve Onion's `type`, `imgpath`, and unknown fields when toggling favorites.
- Use backup + temporary write + flush/fsync + atomic replace where the filesystem supports it.

**Pass:** round-trip fixtures are byte/semantically equivalent except for the exact intended field change.

### P0-4 — Repair health/stress evidence

- Replace PocketOS proc scanning with line-by-line key parsing equivalent in rigor to the new Onion baseline monitor.
- Unit-test realistic `/proc/self/status` and `/proc/meminfo` fixtures.
- Give each stress/baseline run a clear session boundary so old samples do not contaminate comparison.
- Exercise Browse games, Library games, Favorites, Settings, fonts, themes, and game-options rendering—not only top-level categories.
- Make the terminal runner establish the same runtime environment PocketOS normally receives.
- Make the report compare like-for-like PocketOS/Onion runs rather than only printing independent ranges.

**Pass:** real-device PocketOS and Onion CSVs both contain valid RSS and MemAvailable values throughout comparable runs.

## Phase 1 — Rebuild the ROM pipeline around Onion contracts

### Canonical system registry

Create one data owner used by installer, importer, genre scanner, and tests.

For each system define:

- canonical Onion ROM folder;
- accepted aliases;
- extensions;
- whether ZIP is the ROM itself or an archive to inspect;
- multi-file rules;
- OpenVGDB system name if supported;
- ambiguity policy.

Do not create a new system folder from an ambiguous extension without explicit user choice.

### Import transaction model

- Scan and produce a plan first.
- Show source → destination and collisions.
- Stream large files rather than `read()`ing entire archive members.
- Preserve related files/directories where required.
- Write to staging names then atomically promote when practical.
- Never overwrite an existing ROM silently.
- On partial failure, report exactly what was added and leave pre-existing files untouched.

### Hashing and identification

- Stream the entire file for CRC32.
- For formats where CRC is not a reliable identity signal, fall back deliberately rather than pretending a partial CRC is valid.

### Remove executable data configuration

- Replace `exec(fix_unsorted.py)` with JSON/TOML/static packaged data or a shared Python data module that is intentionally included.

### OpenVGDB acquisition

- Fix the official v29.0 asset URL (`openvgdb.zip`).
- Add a first-run/download path shared by Setup Suite and standalone tools.
- Verify downloaded ZIP/database structure before use.
- Cache the DB locally rather than bundling it until redistribution terms are established.
- Correct README claims to match actual packaged behavior.

**Phase proof:** representative fixtures import to the same locations Onion expects, with no destructive cleanup and correct metadata generation.

## Phase 2 — Make safety executable in CI

Add a normal CI workflow on:

- pull requests;
- pushes to `main`.

Required lanes:

1. Python unit/integration tests.
2. ARM compile + Makefile compatibility checks.
3. Host UI render tests.
4. ROM importer/genre fixture tests.
5. Onion launch-contract adversarial tests.
6. Metadata-preservation tests.
7. Installer install/update/uninstall transaction tests.
8. Health parser/comparison tests.
9. Static checks where compatible (`shellcheck` for shell scripts; C static analysis/fortify investigation).

Replace high-value source-string contract tests with behavior tests where practical. Source checks may remain as lightweight guardrails, not the main proof.

Protect `main` and require the CI workflow before merge. Release jobs should build from a commit that already has the required CI result rather than making the release workflow the first full gate.

**Phase proof:** intentionally reintroducing each P0 defect causes CI to fail.

## Phase 3 — Runtime and installer robustness

- Make SD-card selection fail closed: invalid selection re-prompts; never silently choose candidate 0.
- Detect and display Onion version explicitly during install.
- Bind runtime-patch compatibility tests to the current Onion `runtime.sh` fixture/hash/contract.
- Refresh or version runtime backups so uninstall never restores stale launch logic across an Onion update.
- Dynamically discover installed Onion apps instead of showing a static list as “installed.”
- Surface truncation when fixed-size launcher arrays hit capacity.
- Audit all `system()`/`popen()` calls and replace those that only perform file/device operations with direct APIs.
- Harden `system.json` editing against oversized/malformed files and use the same durable write primitive.

## Phase 4 — Refactor after contracts are pinned

Only after P0–P2 pass:

Split `pocketOS.c` along existing responsibility boundaries, for example:

- `main.c` / state loop;
- `onion_launch.c`;
- `library.c`;
- `favorites.c`;
- `settings.c`;
- `theme.c`;
- `audio.c`;
- `render.c`;
- `logging.c`.

Keep refactors behavior-preserving and small. Do not mix a major module move with new product behavior.

**Phase proof:** identical fixture/host-render behavior before and after each extraction, with ARM build still passing.

## Phase 5 — Device qualification and release proof

Use a sacrificial/test SD card.

Run:

1. Clean Onion v4.3.1-1 + PocketOS install.
2. Launch/fallback test.
3. Representative import matrix.
4. Genre scan with pre-populated metadata-rich game lists.
5. Favorite add/remove with image/type metadata present.
6. Settings mutation + reboot persistence.
7. App launch/return paths.
8. Screenshot path.
9. Comparable PocketOS and stock-Onion active monitoring runs.
10. PocketOS update over an existing install.
11. Uninstall and verify stock Onion behavior/data remain intact.

Record exact release commit, test SD baseline, failures, both health reports, and any residual difference that cannot be explained.

## Checkpoints / re-plan triggers

At each phase boundary choose `CONTINUE`, `CHANGE`, `CUT SCOPE`, `RESEARCH`, or `STOP` from evidence.

Re-plan if:

- current Onion behavior contradicts an assumed contract;
- a safe generic importer cannot be built for an ambiguous format without user input;
- third-party database redistribution terms are unclear;
- FAT/exFAT filesystem behavior invalidates an assumed atomic-write strategy;
- on-device evidence differs materially from host/fixture behavior;
- safety fixes require a product-scope change rather than an implementation change.

## Recommended execution order

1. P0-1 destructive cleanup
2. P0-2 launch command security
3. P0-3 metadata preservation
4. P0-4 health/stress evidence
5. Phase 1 ROM/genre pipeline
6. Phase 2 CI + branch protection
7. Phase 3 runtime/installer robustness
8. Phase 4 C modularization
9. Phase 5 hardware qualification

Do not advance a P0 item merely because its code changed. Advance it only when its failure reproduction has become a regression test and the test passes on the corrected implementation.
