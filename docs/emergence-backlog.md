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

## Current focus

PocketOS remains an Onion-integrated launcher. These discoveries are intended
to improve PocketOS's own reliability, observability, and user experience;
they are not a proposal to fork or replace Onion OS.
