# Task: RCV1-T04 — Phase 1 orchestration reconciliation

- Status: `NEEDS_SHAPING`
- AGI status: `UNCHECKED` — run after parent roadmap approval and T01/T02/T03 completion
- Type: `PLANNING`
- Owner: Orchestration Operator
- Risk: `HIGH`
- Goal: reconcile the three first-wave evidence packages into one authoritative Phase 1 state, then shape the smallest repair/research tasks needed to satisfy the Phase 1 exit condition.
- Parent roadmap: [`../../roadmap-release-confidence-v1.md`](../../roadmap-release-confidence-v1.md) — proposed MAPS_L execution revision, not yet approved
- Related records:
  - `RCV1-T01`
  - `RCV1-T02`
  - `RCV1-T03`
  - [`../../risk-register-release-confidence-v1.md`](../../risk-register-release-confidence-v1.md)
- Autonomous continuation: `YES` after parent approval

## Inputs and source of truth

- Inputs: all three first-wave evidence outputs plus current live repository state at reconciliation time.
- Authoritative sources: current implementation/tests/workflows/live Git state; helper outputs are evidence packages, not independent authority.
- Evidence labels: preserve helpers' labels and resolve conflicts explicitly.
- Dependencies / preconditions: T01/T02/T03 complete and operator-verification sampling passed.

## Change boundary

- MAY CHANGE: parent roadmap status/details inside the approved objective; issue #14 planning/status; create bounded next-wave task records; correct planning/evidence metadata that is demonstrably stale.
- MUST NOT CHANGE: production/runtime code, release workflow behavior, public distribution state, GitHub Releases/tags, branch protection, or high-risk implementation as part of this planning task.
- MAY CHANGE IF NECESSARY: planning/evidence cross-links and task boundaries needed to remove contradictions.
- HUMAN REAUTHORIZATION REQUIRED: material expansion beyond Release Confidence V1 objective/envelope or any new destructive/external/publication authority.

## Decision authority

- Inherited roadmap authority: integrate evidence, choose sequencing/parallelism, shape child tasks, route workers/reviewers, and make in-envelope `CONTINUE | CHANGE | CUT SCOPE | RESEARCH | STOP` checkpoint decisions.
- Owner may decide:
  - which helper finding controls when evidence differs;
  - which repair tasks can run in parallel;
  - which findings are release blockers versus non-blocking maintenance;
  - which worker class should own each next task;
  - whether a finding belongs in Phase 1 or should be routed to a later named phase.
- Resolve internally first: contradictions through authoritative evidence, safe inspection, focused helper, then independent challenge when consequential.
- Human escalation only if: resolution would change the approved objective/scope/permission envelope or requires a human-only product/release preference.

## Acceptance criteria

- [ ] T01/T02/T03 outputs are reconciled into one Phase 1 truth state with no silent contradiction.
- [ ] Every `UNKNOWN`, `STALE`, divergence risk, and fail-open release gap has one disposition: repair task, research task, later-phase route, accepted non-blocker with rationale, or human-boundary decision.
- [ ] Version-identity canonicalization is either shaped into a bounded implementation task or explicitly blocked on a human policy decision.
- [ ] No implementation task mixes unrelated risk surfaces merely to reduce task count.
- [ ] Next-wave tasks identify owner role, worker class, dependencies, output paths, acceptance criteria, verification, review, and continuation.
- [ ] Phase 1 exit condition is objectively testable after the shaped repairs complete.
- [ ] A fresh independent challenger reviews the reconciliation before Phase 1 is declared complete.

## Verification and evidence

- Verification: compare reconciliation decisions back to every first-wave finding; ensure no material finding disappears without explicit disposition.
- Evidence to preserve: updated roadmap/task records + issue #14 status + independent challenge verdict.
- Review required: `INDEPENDENT_REVIEW` using a fresh context packet containing task contract, helper outputs, reconciliation, and acceptance criteria—not the orchestrator's hidden reasoning.

## Conditional execution rules

- Environment / target: current repository state after first-wave evidence work.
- Ordered procedure:
  1. validate helper outputs;
  2. resolve conflicts/duplicates;
  3. classify release blockers;
  4. update risk register if new risks appear;
  5. shape next-wave repair/research tasks to AGI readiness;
  6. dispatch independent challenger on reconciliation;
  7. route changes if requested;
  8. once approved, dispatch next eligible tasks without routine human pause.
- Failure branches:
  - IF evidence conflicts materially THEN stop only affected branch and resolve using MAPS_L conflict rules.
  - IF a helper output is insufficient THEN reassign/narrow that helper task rather than guessing.
  - IF independent review requests changes THEN route correction and re-review before Phase 1 exit.
- Rollback / recovery: revert planning-state edits if reconciliation is invalidated; source/runtime remains untouched by this task.
- External side effects: none beyond routine planning/status GitHub updates already inside approved roadmap authority.
- Effort limit: if reconciliation reveals broad redesign rather than bounded repair, checkpoint `CHANGE` or `HUMAN REAUTHORIZATION` as appropriate before implementation.
- Operational independence: `REQUIRED` — the durable roadmap/tasks must be sufficient for a fresh orchestration operator to continue without this chat.
- Reproduction package: helper evidence outputs + reconciliation dispositions + next-wave task records + review verdict.

## Completion / handoff

- Completed: authoritative Phase 1 state and AGI-ready next wave.
- Not completed: next-wave repairs themselves.
- Current blocker: none unless evidence conflict or authority boundary remains unresolved.
- Next eligible roadmap task: first unblocked next-wave repair/research task selected by orchestration operator.
- Human action required: none unless the reconciliation identifies a true permission/objective boundary.
