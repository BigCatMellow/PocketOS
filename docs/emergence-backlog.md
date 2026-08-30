# PocketOS Emergence Backlog

State: discovery capture only. Items here do not authorize implementation or
change PocketOS's Onion integration boundary. Promote an item only through a
bounded task with observable DONE criteria.

This is the single owner for cross-cutting observations that arise while using
and validating PocketOS. It follows MAPS_L E/I: observe, connect, synthesize,
name, test, then deliberately promote.

## E-01 — Runtime provenance

**Observation.** The active device runtime can contain useful diagnostics
outside PocketOS's managed hook. The launcher therefore has behavior whose
exact source revision is not automatically evident from a PocketOS log.

**Value.** A support investigation can establish exactly which Onion runtime,
managed hook, and PocketOS build ran without copying or modifying the runtime.

**Smallest safe experiment.** On install and launcher start, write a bounded,
read-only manifest containing the Onion runtime SHA-256, managed-block
SHA-256, PocketOS binary SHA-256, and detected Onion version.

**Promotion bar.** Verify that the manifest survives a stock Onion runtime
upgrade and distinguishes a managed-hook update from a runtime-base change.

## E-02 — Unclean session classification

**Observation.** A graceful PocketOS exit can be logged, but power loss,
SIGKILL, or a device reset may leave no final application record.

**Value.** The next launcher start can distinguish a clean return from an
interrupted session without guessing or attempting automatic repair.

**Smallest safe experiment.** Append one `session_start` record and one
`session_end` record with a runtime-generated ID. At the next start, report an
unmatched prior start as `previous_session_unclean`; do not mutate user data or
change boot behavior.

**Promotion bar.** Reproduce both a clean exit and an interrupted process in a
fixture, and prove the classifier never labels a clean session as unclean.

## E-03 — Cross-layer handoff correlation

**Observation.** PocketOS and Onion runtime logs currently describe adjacent
parts of a launch, but not necessarily with one shared session identity.

**Value.** A short black-screen or return-to-Onion event can be reconstructed
as one timeline: input, PocketOS exit reason, runtime status, and command
handoff state.

**Smallest safe experiment.** Have the runtime export `POCKETOS_SESSION_ID`;
PocketOS records it at start and exit, while the runtime records it with child
status and handoff presence.

**Promotion bar.** A behavioral runtime test proves that successful launch,
normal return without a command, and fail-open fallback produce distinct,
correlatable records.

## E-04 — Actual display and power-state evidence

**Observation.** Health logs capture configured brightness, not necessarily
the physical backlight, framebuffer, or device power state that a user sees.

**Value.** A black screen can be classified as an application exit, display
failure, backlight change, or device power event rather than treated as one
generic "crash."

**Smallest safe experiment.** On a real device, read candidate display and
power sysfs values at launch, before exit, and after return. This experiment is
read-only and must first establish which values are stable and meaningful on
the supported hardware.

**Promotion bar.** Keep only measurements that have a documented device path,
are low-cost, and improve diagnosis in a reproduced black-screen case.

## E-05 — Browse metadata-reader compatibility

**Area.** Frontend / data boundary

**Observation.** Desktop tools safely write `miyoogamelist.xml` with a real XML
tree, while the launcher Browse view reads only line-oriented `<path>`,
`<name>`, and `<genre>` fragments. Valid pretty-printed XML, reordered fields,
or escaped paths can therefore be represented differently by the writer and
the consumer.

**Value.** Browse shows the same user library that the importer and genre
scanner safely maintain, including ordinary Onion/user-authored gamelists.

**Smallest safe experiment.** Add a host fixture containing multi-line,
reordered, and XML-escaped game fields; verify Browse sees the expected title,
genre, and launch path without changing the gamelist.

**Promotion bar.** Select a bounded parser/read-model change only after the
fixture reproduces a visible mismatch on the supported launcher build.

## E-06 — Capacity feedback across all lists

**Area.** Frontend / persistence

**Observation.** System, game, and Browse limits are surfaced, but recent and
favorite loading stops at fixed capacities, and attempting to add a favorite
at the capacity limit has no explicit user feedback. The Most Played query also
intentionally limits its candidate set to 500 rows without exposing that policy
in the UI.

**Value.** A large library never looks complete when PocketOS is showing only a
bounded subset, and an attempted action never appears to have been ignored.

**Smallest safe experiment.** Use fixtures just above each capacity and verify
one visible, truthful indicator for truncation/full capacity while proving the
source JSON and activity database are unchanged.

**Promotion bar.** Define per-list product policy—raise, paginate, or retain
the limit—before changing any fixed array or persistent format.

## E-07 — Large-library interaction latency

**Area.** Frontend / backend performance

**Observation.** Moving between systems synchronously reloads the selected ROM
directory on the UI thread. This is inexpensive on the observed card, but its
cost grows with folders approaching the current game-list limit.

**Value.** Shoulder/D-pad navigation remains responsive in large collections;
the user sees a short loading state rather than an unexplained pause or black
frame.

**Smallest safe experiment.** Create deterministic 100-, 1,000-, and
1,500-file fixtures and record `load_games` duration plus input-to-render time
on host and device. This experiment does not alter a user library.

**Promotion bar.** Promote only if a measured budget is exceeded; choose a
cache, incremental loader, or explicit progress treatment from that evidence.

## E-08 — Behavioral UI journey coverage

**Area.** Frontend verification

**Observation.** The host renderer proves static screens are nonblank and
distinct, while many UI contracts inspect source text. Neither proves a
sequence of real SDL inputs reaches the intended state and never launches a
game unexpectedly.

**Value.** Layout consistency changes can be verified as device behavior, not
only as source structure or screenshots.

**Smallest safe experiment.** Add a host-only SDL event fixture for a short
journey through Browse, Library, Favorites, Settings, and back. Assert the
visited state sequence and that no command file is written without the launch
button.

**Promotion bar.** Keep journeys compact and behavior-focused; do not turn
pixel-perfect screenshots into a brittle universal test suite.

## E-09 — Handheld readability evidence

**Area.** Frontend accessibility

**Observation.** Theme checks establish broad luminance bounds and static
renders, but they do not measure text-to-surface contrast for every role or
legibility on the actual 640×480 handheld display.

**Value.** A theme/font combination remains readable on hardware, especially
for selected, muted, and footer text.

**Smallest safe experiment.** Derive contrast ratios from the theme roles and
render representative small/body/selected text. Flag only clearly inadequate
combinations for manual handheld review.

**Promotion bar.** Adopt a small role-based contrast contract only after
hardware review confirms it tracks readability better than a generic score.

## E-10 — Pull-request UI evidence

**Area.** Backend / release pipeline

**Observation.** Normal CI runs Python/contract tests and an ARM build, while
the host UI renderer runs in the tag-release workflow. A UI regression can
therefore first receive rendered evidence at release time.

**Value.** Frontend changes receive the same early evidence discipline as
backend safety changes.

**Smallest safe experiment.** Run the existing host renderer in a disposable
PR-like environment and measure duration, dependencies, and artifact size.

**Promotion bar.** Add it to normal CI only if the measured cost is acceptable
and it remains a distinct behavioral/render check rather than duplicate
ceremony.

## E-11 — Intentional collections beyond one Favorites list

**Area.** User-facing product behavior

**Observation.** A large, intentionally varied library can make one flat
Favorites list carry several meanings at once: enduring favorites, current
games, homebrew, ROM hacks, and discoveries.

**Value.** PocketOS can help a user return to meaningful groups without
reclassifying, deleting, or modifying the underlying ROM library.

**Smallest safe experiment.** Use the host fixture to prototype read-only
collections such as `Homebrew & Hacks`, `Currently Playing`, and `All-Time`;
evaluate D-pad navigation and whether each collection has an explainable,
non-destructive membership rule.

**Promotion bar.** Choose a single data owner and an import/export-compatible
format before any collection becomes persistent user data.

## E-12 — System-scoped Favorites

**Area.** Frontend / existing favorite data

**Observation.** Every loaded favorite already has a system derived from its
Onion launcher path, but Favorites is rendered as one alphabetical list with a
small system badge. The Library already has a familiar two-pane system → game
interaction model.

**Value.** A large Favorites list can be navigated by system without requiring
the user to remember titles, scroll through unrelated platforms, or maintain a
second collection format.

**Smallest safe experiment.** In the host fixture, render Favorites as the
Library-style left pane `All + systems with favorite counts` and a right pane
of only the selected system's favorites. Start on `All`; retain the current
favorite launch and remove actions. The existing line-delimited
`Roms/favourite.json` remains the sole persistent owner.

**Promotion bar.** Prove a D-pad journey can move from `All` to a system,
launch the same favorite command as today, remove a favorite, and return with
correct counts. Entries with an unknown launcher-derived system must remain
visible under `Other`, never disappear.

## Current focus

PocketOS remains an Onion-integrated launcher. These discoveries are intended
to improve PocketOS's own reliability, observability, and user experience;
they are not a proposal to fork or replace Onion OS.
