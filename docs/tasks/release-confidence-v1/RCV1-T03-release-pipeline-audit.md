# Task: RCV1-T03 — Release pipeline gate audit

- Status: `NEEDS_SHAPING`
- AGI status: `UNCHECKED` — run after parent roadmap approval
- Type: `RESEARCH`
- Owner: Evidence Investigator C
- Risk: `HIGH`
- Goal: map the current candidate-build and publication path, identify which release-critical gates are automated versus manual/prose-only, and determine where the pipeline can fail open.
- Parent roadmap: [`../../roadmap-release-confidence-v1.md`](../../roadmap-release-confidence-v1.md) — proposed MAPS_L execution revision, not yet approved
- Related records: [`../../project-assessment-2026-09-16.md`](../../project-assessment-2026-09-16.md)
- Autonomous continuation: `YES` after parent approval

## Inputs and source of truth

- Inputs: `.github/workflows/*`, package/build scripts, environment/release contract files, tests covering release behavior, README/release instructions, current distribution-paused state, and live branch/check state when relevant.
- Authoritative sources: current workflows/scripts/tests and live repository state; historical release behavior is evidence of failure mode, not current authority.
- Evidence labels: `VERIFIED | PARTIAL | UNKNOWN | STALE`.
- Dependencies / preconditions: parent roadmap approved; exact target revision recorded.

## Change boundary

- MAY CHANGE: create/update only `docs/evidence/release-pipeline-audit-v1.md`.
- MUST NOT CHANGE: workflows, package scripts, release settings, branch protection, runtime code, GitHub Releases/tags.
- MAY CHANGE IF NECESSARY: evidence-table structure.
- HUMAN REAUTHORIZATION REQUIRED: none for read-only audit.

## Decision authority

- Inherited roadmap authority: inspect the complete release path and classify gate coverage.
- Owner may decide: whether each gate is automated, manually evidenced, absent, stale, fail-open, or fail-closed based on direct evidence.
- Resolve internally first: pipeline behavior by tracing workflow/script conditions and focused tests.
- Human escalation only if: a required repository/admin setting cannot be inspected and materially changes the conclusion; record `UNKNOWN` rather than assuming.

## Acceptance criteria

- [ ] One ordered release flow exists from source revision → candidate build → checks/evidence → package → approval → publish → post-publish verification.
- [ ] Every release-blocking condition in the parent roadmap is mapped to an enforcement point or marked absent/unknown.
- [ ] Manual evidence gates are distinguished from automated gates.
- [ ] Fail-open paths are explicit, especially stale SHA/review/device/package identity cases.
- [ ] Current distribution-paused mechanism is identified and verified as separate from future publication readiness.
- [ ] Proposed hardening targets are ranked by release-risk reduction, not implementation convenience.

## Verification and evidence

- Verification: orchestration operator traces the audit against actual workflow/script branches and samples each `fail-closed` claim against its condition/test.
- Evidence to preserve: `docs/evidence/release-pipeline-audit-v1.md` with target SHA/date and gate matrix.
- Review required: `OWNER_CHECK`; high-impact proposed gate changes are independently reviewed when later implemented.

## Conditional execution rules

- Environment / target: current PocketOS repository + live GitHub state visible to the connected account.
- Ordered procedure: reconstruct flow → map release-blocking conditions → classify enforcement → identify fail-open gaps → rank hardening targets.
- Failure branches: IF an admin/protection setting is unreadable THEN mark that enforcement `UNKNOWN`, cite the access limitation, and continue auditing independent gates.
- Rollback / recovery: evidence-only task.
- External side effects: none.
- Effort limit: stop at the first complete release path; do not audit unrelated CI/development workflows unless they participate in release evidence.
- Operational independence: `REQUIRED` — another maintainer must be able to repeat the gate audit from recorded paths and checks.
- Reproduction package: target SHA + workflow/script map + gate matrix + unresolved visibility gaps.

## Completion / handoff

- Completed: evidence-backed release pipeline/gate audit.
- Not completed: workflow or contract repairs.
- Current blocker: none unless essential release path is inaccessible.
- Next eligible roadmap task: `RCV1-T04` after T01/T02/T03 all complete.
- Human action required: only if an unreadable admin setting must be changed or exposed to proceed.
