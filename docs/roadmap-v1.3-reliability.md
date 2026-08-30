# PocketOS v1.3 Reliability Roadmap

State: MERGED — follow-up usability work continues separately.

## Definition of DONE
PocketOS is safe-by-default on user ROM libraries, preserves Onion metadata, produces trustworthy health evidence, and blocks regressions before release.

Final proof: PR CI + ARM build + adversarial data-integrity tests + independent review + real-device 30-minute stress run with valid memory telemetry.

## Work arc
- [x] Repair health telemetry parsing; invalid telemetry fails visibly.
- [x] Make ROM variant cleanup analysis-only.
- [x] Preserve gamelist XML metadata and refuse malformed overwrite.
- [x] Compute CRC32 across the complete ROM.
- [x] Add PR/main CI and adversarial regressions.
- [x] Stop invalid SD selection from silently choosing a drive.
- [x] Consolidate canonical Onion system/folder mappings.
- [x] Stop ambiguous disc/archive extensions from silently auto-routing to the wrong system.
- [x] Add explicit user-selected import support for ambiguous CD/arcade/Neo Geo formats.
- [x] Replace executable Python overrides with data-only overrides.
- [x] Stream ZIP extraction with size/free-space/collision limits.
- [x] Make installer and uninstaller transactional across PocketOS-owned/mutated paths.
- [x] Add rollback/recovery regression tests for interrupted install and uninstall.
- [x] Harden the Onion shell handoff against backslash/dollar escape interactions.
- [x] Harden standalone importer/scanner metadata writes and Tk worker boundaries.
- [x] Refresh Onion runtime backup when a newer stock runtime is patched.
- [x] Run real-device 30-minute stress validation with valid memory telemetry (29m59s; RSS 7,464 KB → 8,892 KB after a 9,708 KB cache peak; available memory 75,320–78,176 KB).
- [ ] Split launcher modules only after behavior is locked by tests; this is maintainability work, not a safety prerequisite.

## Verification discipline

Code-side approval evidence must be produced from the current PR head. A green run from an earlier revision does not satisfy the gate after any source, test, workflow, or safety-contract change.

## Release conclusion

The reliability work was independently reviewed, passed required CI and ARM build checks, and was squash-merged to `main` in PR #2. The device-validation gate is satisfied by the recorded Miyoo run. The remaining unchecked items are future feature and maintainability work, not release blockers for this reliability arc.
