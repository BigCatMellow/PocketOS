# Task: RCV1-T01 — Release-relevant claim inventory

- Status: `NEEDS_SHAPING`
- AGI status: `UNCHECKED` — run after parent roadmap approval
- Type: `RESEARCH`
- Owner: Evidence Investigator A
- Risk: `MEDIUM`
- Goal: produce a finite evidence-backed inventory of current PocketOS claims that materially affect user data, compatibility, installation, reliability, or release trust.
- Parent roadmap: [`../../roadmap-release-confidence-v1.md`](../../roadmap-release-confidence-v1.md) — proposed MAPS_L execution revision, not yet approved
- Related records: [`../../project-assessment-2026-09-16.md`](../../project-assessment-2026-09-16.md)
- Autonomous continuation: `YES` after parent approval

## Inputs and source of truth

- Inputs: current `README.md`, project assessment, v1.3 reliability roadmap, release-confidence roadmap, relevant UI/help text, installer/importer/scanner behavior, tests/contracts, and live repository state where needed.
- Authoritative sources: current implementation/tests/contracts beat historical prose; live repository state beats stale status text.
- Evidence labels: `VERIFIED | PARTIAL | UNKNOWN | STALE`.
- Dependencies / preconditions: parent roadmap approved; current `main`/target revision recorded before inspection.

## Change boundary

- MAY CHANGE: create/update only `docs/evidence/release-claims-v1.md` on the task branch.
- MUST NOT CHANGE: runtime code, tests, workflows, README claims, roadmap conclusions, release settings, GitHub Releases/tags.
- MAY CHANGE IF NECESSARY: task-local evidence formatting only.
- HUMAN REAUTHORIZATION REQUIRED: none for the read-only investigation; any proposed project-scope expansion is reported, not executed.

## Decision authority

- Inherited roadmap authority: read/inspect current PocketOS sources and preserve bounded evidence.
- Owner may decide: which materially release-relevant claims belong in the finite inventory and what evidence directly supports each classification.
- Resolve internally first: ambiguous claim wording via implementation/tests and focused source inspection.
- Human escalation only if: a required source cannot be accessed without a new permission/credential boundary.

## Acceptance criteria

- [ ] Inventory is finite and excludes cosmetic/non-release claims.
- [ ] Every row contains claim, claim source, implementation/source owner, evidence path, release relevance, epistemic state, and required next check/repair.
- [ ] Historical claims are not silently treated as current truth.
- [ ] Material contradictions are explicitly recorded rather than resolved by guess.
- [ ] Strongest alternative interpretation is noted for disputed/high-risk claims.

## Verification and evidence

- Verification: orchestration operator samples every `VERIFIED` row against cited source/evidence and checks all `UNKNOWN/STALE` rows have an actionable next check.
- Evidence to preserve: `docs/evidence/release-claims-v1.md` with target revision/date.
- Review required: `OWNER_CHECK` by orchestration operator; consequential contradictions may be routed to independent challenge.

## Conditional execution rules

- Environment / target: current PocketOS repository; record exact inspected revision.
- Ordered procedure: inspect claims → trace implementation/evidence → classify → record contradiction/unknown → stop when finite release-relevant universe is covered.
- Failure branches: IF a claim cannot be bound to evidence THEN mark `UNKNOWN`, record exact missing evidence, do not infer.
- Rollback / recovery: evidence-only task; revert task output if malformed.
- Security / privacy controls: do not record secrets or private user data.
- External side effects: none.
- Effort limit: stop and ask orchestration operator to narrow if inventory exceeds ~30 material claims without clear release relevance.
- Operational independence: `REQUIRED` — output must contain enough source paths and method that another investigator can reproduce classifications.
- Reproduction package: target SHA + source-path list + evidence table + verification notes.

## Completion / handoff

- Completed: finite claim inventory with evidence classifications.
- Not completed: repairs to claims/implementation.
- Current blocker: none unless a required source is inaccessible.
- Next eligible roadmap task: `RCV1-T04` after T01/T02/T03 all complete.
- Human action required: none unless access/authority boundary is hit.
