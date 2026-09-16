# PocketOS Release Confidence V1

Status: ACTIVE ROADMAP / PUBLIC DISTRIBUTION PAUSED

Owner: PocketOS project

Purpose: bring release truth, high-risk behavior evidence, provenance, and publication gates up to the standard of the current implementation before any new public PocketOS release.

## Objective

Produce a fresh PocketOS release candidate whose exact source, safety behavior, device evidence, review record, packaged contents, and publication decision are all recoverable and mutually consistent.

## Definition of DONE

Release Confidence V1 is complete only when all of the following are true:

- the candidate is built from one exact frozen commit;
- all required automated tests and the Miyoo ARM build pass on that exact commit;
- high-risk ROM/import/install/uninstall/metadata/fallback behaviors have focused evidence;
- any required real-device run retains its raw evidence plus a human-readable summary;
- an independent review is durably recoverable and bound to the exact candidate revision;
- package contents identify the source revision and match the reviewed source/contract;
- artifact hashes are generated and verified;
- no unresolved release-critical claim is `UNKNOWN`, contradicted, or supported only by stale evidence;
- explicit human release authority is recorded after the above gates pass;
- only then is public release publishing re-enabled.

## Non-goals

Until this roadmap is complete:

- do not add major feature families;
- do not perform a broad rewrite;
- do not split modules solely for aesthetics;
- do not revive or republish an old release package;
- do not treat green unit tests as a substitute for device or provenance evidence where those are actually required.

## Phase 0 — contain misleading distribution

Status: COMPLETE

- [x] Pause automatic public release publishing.
- [x] Remove installation/download promotion from README.
- [x] Remove all previously published GitHub Release objects and attached assets.
- [x] Preserve historical Git tags for traceability.
- [x] Remove the temporary purge workflow after execution.

Exit condition: no packaged PocketOS download is presented as currently approved.

## Phase 1 — reconcile project truth

Priority: HIGH

### What to do

- [ ] Build a finite inventory of release-relevant user-facing claims from README, roadmap, UI/help text, installer/importer behavior, and release metadata.
- [ ] For each claim, identify the exact source owner and whether current `main` actually implements it.
- [ ] Mark claims as `VERIFIED`, `PARTIAL`, `UNKNOWN`, or `STALE` rather than inferring completion from roadmap checkboxes.
- [ ] Remove or qualify stale completion/release-readiness wording.
- [ ] Establish one canonical version/release identity source so code, environment contract, package metadata, and release title cannot silently diverge.

### How

Use a small claim table with:

```text
claim
-> source file / implementation
-> focused test or evidence
-> release relevance
-> current epistemic state
-> required next check
```

Do not create a giant compliance inventory. Focus on claims that affect user data, installability, compatibility, reliability, or release trust.

Exit condition: project documentation no longer says more than the implementation/evidence supports.

## Phase 2 — verify high-risk behavior boundaries

Priority: HIGH

The dangerous surfaces are operations that can alter user state or make system/platform inferences.

### A. ROM import and variant handling

- [ ] Prove ambiguous disc/archive formats are not silently routed to a guessed platform.
- [ ] Prove explicit target selection preserves intended directory/layout structure.
- [ ] Prove variant analysis is non-destructive on current candidate behavior.
- [ ] Prove ZIP extraction remains bounded by size, free-space, path, and collision rules.
- [ ] Add/retain adversarial fixtures for malformed archives, collisions, ambiguous formats, and variant sets.

### B. Metadata/data integrity

- [ ] Prove unrelated gamelist/XML metadata survives edits.
- [ ] Prove malformed metadata is refused rather than overwritten.
- [ ] Prove writes are atomic/recovery-safe where the project claims they are.
- [ ] Prove no unnecessary rewrite occurs for unchanged data where that matters to integrity.

### C. Install/update/uninstall

- [ ] Verify explicit SD-card selection and refusal of unsafe/ambiguous targets.
- [ ] Verify interrupted install and uninstall rollback/recovery paths.
- [ ] Verify PocketOS mutates only the paths it owns or explicitly declares.
- [ ] Verify Onion fallback remains usable when PocketOS launch/handoff fails.

### D. Shell/runtime handoff

- [ ] Keep focused tests for quoting/escaping boundaries.
- [ ] Verify backup/restore behavior on a disposable or cloned test card.

### How

Prefer deterministic fixture tests first. Use disposable/cloned SD-card tests for effects that cannot be honestly established in a host fixture. Never use the user's only library/card as the first destructive-path test environment.

Exit condition: every high-risk behavior has a specific test/evidence path and no safety claim depends only on code inspection.

## Phase 3 — repair evidence provenance

Priority: HIGH

### Raw device evidence

- [ ] Define the exact telemetry/log artifact required for a release-candidate device run.
- [ ] Store the raw artifact or a durable immutable attachment referenced from the release evidence record.
- [ ] Record device model, Onion version, candidate SHA, start/end time, procedure, anomalies, and summary metrics.
- [ ] Keep conclusions proportional: one 30-minute run is useful bounded evidence, not a lifetime-stability guarantee.

### Independent review

- [ ] Define what requires independent review for a release candidate.
- [ ] Require the review to identify the exact substantive candidate SHA.
- [ ] Preserve the review in a durable PR review, issue, or review-evidence file.
- [ ] Do not let the implementation author silently stand in for independent review.

Exit condition: a fresh maintainer can recover primary evidence without relying on chat memory or an unchecked roadmap statement.

## Phase 4 — harden the release contract

Priority: HIGH

The release system should fail closed when release-critical evidence is missing.

### Candidate identity

- [ ] Freeze an exact candidate commit.
- [ ] Embed or package that commit SHA with the release metadata.
- [ ] Ensure version identifiers agree across source, environment contract, package, and release metadata.

### Automated gates

- [ ] Require exact-head `test` success.
- [ ] Require exact-head `arm-build` success.
- [ ] Require release/package integration tests.
- [ ] Require package checksum generation and verification.
- [ ] Verify the final package contains the expected runtime/install/importer components from the candidate source.

### Manual/high-risk gates

- [ ] Represent required device evidence and independent-review evidence as explicit release inputs/attestations rather than prose assumptions.
- [ ] Make publication refuse to run when those required records are absent or bound to another SHA.
- [ ] Separate `build candidate` from `publish release` so packages can be inspected before public distribution.

### Publication authority

- [ ] Require explicit human release approval after all evidence is assembled.
- [ ] Re-enable public publishing only as part of the reviewed release change, not as an unrelated toggle.

Exit condition: the normal path cannot accidentally publish an old, unreviewed, or differently tested artifact.

## Phase 5 — maintainability after behavior lock

Priority: MEDIUM

Only after Phases 1–4 stabilize behavior:

- [ ] identify stable boundaries inside `src/pocketOS/pocketOS.c`;
- [ ] split only modules whose separation improves reviewability/testability;
- [ ] preserve behavior with focused tests before each split;
- [ ] avoid a broad architecture rewrite during release-confidence work.

Candidate boundaries may include navigation/state, rendering, library/model access, settings, telemetry, and Onion handoff, but actual splits should follow current code cohesion rather than this list mechanically.

Exit condition: maintainability improves without mixing behavioral redesign into release stabilization.

## Phase 6 — fresh release candidate

Priority: FINAL

- [ ] Cut a release-candidate branch or exact candidate revision from the stabilized tree.
- [ ] Run the complete automated gate set on that exact revision.
- [ ] Run required disposable-card/device validation and retain raw evidence.
- [ ] Obtain durable independent review bound to the exact candidate revision.
- [ ] Build final artifacts once from the approved candidate.
- [ ] Verify hashes, contents, source identity, and installation instructions.
- [ ] Record explicit human release approval.
- [ ] Re-enable public publishing and publish the fresh release.
- [ ] Immediately verify the public assets correspond to the approved candidate and documented hashes.

## Release-blocking conditions

Do not publish if any of these are true:

- source/candidate SHA is ambiguous;
- release package was built from a different revision than the reviewed candidate;
- required CI/ARM checks are stale or failed;
- destructive/data-integrity behavior has unresolved evidence gaps;
- required raw device evidence is missing;
- required independent review is not recoverable or is bound to another revision;
- version/package/release metadata disagree;
- distribution instructions describe behavior not present in the candidate;
- explicit human release authority has not been recorded.

## Priority order

```text
1. Truth reconciliation
2. High-risk behavior proof
3. Evidence provenance
4. Release-contract hardening
5. Fresh candidate verification
6. Maintainability cleanup
7. New feature expansion
```

The key principle is simple: **the downloadable product must never be less trustworthy than the source tree used to justify it.**
