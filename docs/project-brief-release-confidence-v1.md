# Project Brief: PocketOS Release Confidence V1

- Owner: human PocketOS project owner
- Status: `DRAFT`
- Goal: make PocketOS release truth, safety evidence, review provenance, and packaged-artifact identity trustworthy enough to support a fresh public release.
- User/operator: PocketOS project owner, future maintainers, and PocketOS users who need the downloadable product to match the safety claims made by the source tree.

## Current reality

- Checked facts:
  - public distribution is paused;
  - previously published GitHub Release objects/download assets were removed while historical tags were preserved;
  - automatic public release publishing is disabled;
  - protected `main` requires `test` and `arm-build` checks;
  - the historical `v1.2.8` package predated later safety hardening present on `main`;
  - current project assessment identifies unresolved evidence-provenance and release-contract weaknesses;
  - major feature work, including the parked LÖVE/Love2D idea, is not active scope.
- Evidence/source paths:
  - [`project-assessment-2026-09-16.md`](project-assessment-2026-09-16.md)
  - [`roadmap-release-confidence-v1.md`](roadmap-release-confidence-v1.md)
  - [`future-ideas.md`](future-ideas.md)
  - GitHub issue `#14`
  - live PocketOS repository/CI state
- Important assumptions:
  - the current safety hardening can be verified and packaged without a broad rewrite;
  - a fresh release can be made trustworthy by tightening evidence, exact-revision binding, and publication gates rather than redesigning the product;
  - real-device validation will remain necessary for claims that host tests cannot establish.

## Definition of DONE

- Finished result: a fresh PocketOS release candidate exists whose exact source revision, high-risk behavior evidence, device evidence, independent review, package contents, hashes, and publication decision are mutually consistent and recoverable.
- Final proof:
  1. exact candidate revision frozen;
  2. exact-head `test` and `arm-build` pass;
  3. required high-risk behavior checks pass;
  4. required raw device evidence is retained and bound to the candidate;
  5. durable independent review approves the exact candidate;
  6. final artifacts are built from that candidate and package/source identity plus hashes verify;
  7. explicit human release approval is recorded;
  8. after publication, public assets are verified against the approved candidate and hashes.
- Final proof performed/inspected by: orchestration operator + independent reviewer + device validation operator for hardware evidence + human project owner for final release authority.

## Scope and boundaries

- In scope:
  - reconcile release-relevant claims with implementation/evidence;
  - define and verify high-risk ROM/import/metadata/install/uninstall/handoff behavior;
  - preserve raw and summarized evidence;
  - harden candidate/package/release identity and release gates;
  - make bounded code/test/workflow/documentation corrections required by the above;
  - perform maintainability cleanup only after behavior is locked and only where it materially improves reviewability/testability;
  - produce and verify one fresh release candidate.
- Not doing:
  - major new feature families;
  - broad rewrite;
  - LÖVE/Love2D implementation;
  - aesthetic-only architecture cleanup;
  - republishing an old release package;
  - destructive testing on the user's only SD card/library;
  - public publication before the explicit final human release checkpoint.
- Effort limit: if a single blocker consumes three materially different failed repair attempts, or evidence shows the plan requires a broad architectural rewrite, checkpoint and re-plan before continuing that branch.

## Constraints and quality bar

- Evidence outranks completion prose.
- Current exact revision must be recoverable whenever CI/review/package/device evidence is used.
- High-risk safety claims require focused evidence, not code inspection alone.
- Independent review must be meaningfully independent of the implementer and bound to the reviewed revision.
- Repeatable release/device procedures must leave a durable reproduction package.
- The downloadable product must never be less trustworthy than the source tree used to justify it.

## Unknowns and risks

- Highest-risk unknown: whether all current high-risk behavior and release claims can be proved from the stabilized tree without discovering another implementation defect or missing release-critical gate.
- Research/prototype needed first: Phase 1 evidence inventory and release/version identity map.
- Evidence that would invalidate the current plan:
  - current `main` still contains destructive/ambiguous behavior inconsistent with the intended safety contract;
  - the release package cannot be deterministically tied to one source revision;
  - device-only behavior fails in a way that requires product redesign rather than bounded repair;
  - release-confidence work requires material expansion outside the approved objective.

## Decision path

- Human project owner decides:
  - approval/revision of the project objective and permission envelope;
  - any material feature/scope expansion;
  - any non-preauthorized destructive/external action;
  - final public release approval.
- Orchestration operator may decide after roadmap approval:
  - task shaping, sequencing, parallelization, helper/reviewer routing, bounded technical choices, evidence reconciliation, in-envelope re-planning, and whether checkpoints result in `CONTINUE`, `CHANGE`, `CUT SCOPE`, `RESEARCH`, or `STOP`.
- Task owners decide:
  - bounded implementation/investigation choices explicitly delegated by their AGI-ready task contract.
- Independent reviewer decides:
  - review verdict only; reviewer does not become parent owner or expand scope.

## Planning

- Roadmap: [`roadmap-release-confidence-v1.md`](roadmap-release-confidence-v1.md)
- Roadmap state: `DRAFT` pending approval of the upgraded MAPS_L execution model
- Mission meeting required: `YES` — satisfied for initial shaping by the 2026-09-16 project assessment plus this MAPS_L planning pass; approval still required before treating the new envelope as standing autonomous authority.
- First wave: `RCV1-T01`, `RCV1-T02`, `RCV1-T03`, followed by orchestration reconciliation `RCV1-T04`.
- Reconsider if: a core safety assumption fails, the effort limit triggers, real-device evidence contradicts host evidence, or release trust cannot be established without a material objective/scope expansion.
