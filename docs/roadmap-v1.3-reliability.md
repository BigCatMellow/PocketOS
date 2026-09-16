# PocketOS v1.3 Reliability Roadmap

State: HISTORICAL RELIABILITY ROADMAP — implementation work merged; **not current release approval**.

Current release-readiness owner: [`roadmap-release-confidence-v1.md`](roadmap-release-confidence-v1.md)

## 2026-09-16 status clarification

This roadmap records the reliability implementation arc that was merged in PR #2. Its original completion/release conclusion is preserved below as historical context, but subsequent project evaluation found two important provenance/release-truth gaps:

- the last published release line (`v1.2.8`, since removed) predated later safety hardening on `main`;
- the summarized 29m59s device run and claimed independent review could not both be traced to complete primary/durable evidence in the inspected repository/PR record.

Those findings do **not** establish that the device run or review never occurred. They mean the surviving provenance is insufficient to use this roadmap alone as current release authorization.

Public distribution is therefore paused until the Release Confidence V1 roadmap establishes a fresh exact candidate and recoverable evidence chain.

## Definition of DONE
PocketOS is safe-by-default on user ROM libraries, preserves Onion metadata, produces trustworthy health evidence, and blocks regressions before release.

Original intended final proof: PR CI + ARM build + adversarial data-integrity tests + independent review + real-device 30-minute stress run with valid memory telemetry.

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
- [x] Record a real-device 30-minute stress-validation summary (29m59s; RSS 7,464 KB → 8,892 KB after a 9,708 KB cache peak; available memory 75,320–78,176 KB). **Primary raw telemetry provenance must be re-established or rerun for fresh release confidence.**
- [ ] Split launcher modules only after behavior is locked by tests; this is maintainability work, not a safety prerequisite.

## Verification discipline

Code-side approval evidence must be produced from the current PR head. A green run from an earlier revision does not satisfy the gate after any source, test, workflow, or safety-contract change.

For release-critical claims, a completion checkbox is not sufficient evidence by itself. The evidence must be recoverable and bound to the revision it supports.

## Historical release conclusion

The original project record stated that the reliability work was independently reviewed, passed required CI and ARM build checks, was merged to `main` in PR #2, and that the recorded Miyoo run satisfied the device-validation gate.

That statement is now treated as a **historical claim**, not a current release gate. Subsequent review could recover strong code/CI evidence and the summarized device metrics, but not the complete independent-review/raw-device provenance required for a fresh public release decision.

Current release readiness must be established through [`roadmap-release-confidence-v1.md`](roadmap-release-confidence-v1.md).
