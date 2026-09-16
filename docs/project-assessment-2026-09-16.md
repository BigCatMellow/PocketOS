# PocketOS Project Assessment — 2026-09-16

Status: CURRENT PROJECT ASSESSMENT / NOT A RELEASE APPROVAL

Assessment target: `BigCatMellow/PocketOS`

Reference `main` at assessment closeout: `31c93babf487b6c82665022a9e8605a888b6745b`

## Summary

PocketOS is a strong prototype with meaningful device-aware engineering, but its release and evidence discipline has not yet caught up with the sophistication of the implementation.

The project does **not** appear to need a rewrite. The higher-value correction is to make release truth, safety evidence, review provenance, and packaged-artifact identity as rigorous as the current code and test work.

Public distribution is intentionally paused while that work is completed.

## Current containment state

As of 2026-09-16:

- automatic public release publishing is disabled;
- README installation/download promotion is removed;
- all previously published GitHub Release objects and attached downloadable assets were removed;
- historical Git version tags were preserved;
- repository source remains available for inspection and development.

This containment prevents older packaged builds from being mistaken for the safer current development state.

## Strengths observed

PocketOS already has several characteristics that are stronger than a typical hobby prototype:

- Miyoo Mini Plus / Onion-specific ARM build validation rather than host-only confidence;
- required `test` and `arm-build` checks on protected `main`;
- Python and shell regression tests;
- explicit environment/release contracts;
- transactional installer/uninstaller behavior;
- metadata-preservation work;
- ambiguity-safe ROM import direction;
- analysis-only ROM variant handling on current `main`;
- bounded ZIP extraction and storage checks;
- fail-open/fallback thinking around Onion handoff;
- health telemetry and real-device stress-test tooling.

These are good foundations for a release-grade system.

## Material weaknesses / risks

### 1. Shipped-release truth drifted behind `main`

The most important issue found was not simply stale documentation. The last published release (`v1.2.8`, now removed) predated later ROM-safety hardening on `main`.

The historical release importer included behavior that could infer ambiguous disc/platform mappings and included destructive variant-removal paths. Current `main` subsequently moved toward explicit target selection and non-destructive analysis.

This created a dangerous state where inspecting current source could give more confidence than the downloadable product actually deserved.

**Status:** immediate distribution risk contained by pausing publishing and removing all Release downloads. Root release-process cause still requires correction.

### 2. Evidence provenance is weaker than implementation evidence

Some important reliability claims are represented by summaries rather than fully recoverable primary evidence.

Examples identified during the review:

- a 29m59s Miyoo stress run is summarized in project records, but the underlying raw telemetry artifact was not located in the inspected repository/PR evidence;
- the v1.3 reliability roadmap and PR material state that independent review occurred, but a durable independent-review record was not recovered from the inspected GitHub review evidence.

These are recorded as provenance gaps, not as proof that the work did not occur.

The project should distinguish:

```text
test passed
!= device behavior verified

device run summarized
!= raw evidence retained

roadmap says reviewed
!= independent review provenance recoverable
```

### 3. Release automation did not enforce every release-critical gate

The historical release automation performed useful work: tests, ARM build, package construction, checksums, and artifact publication. However, not every manual/high-risk gate was mechanically tied to publication.

A release process should make it difficult to publish a build whose exact source identity, required review, device evidence, or safety verification is missing.

### 4. Completion language became stronger than surviving evidence

The v1.3 roadmap's original conclusion treated independent review and device validation as satisfied release gates. Subsequent evaluation could recover the summary claims but not all primary provenance.

Historical completion statements should therefore be treated as historical project records, not current release authorization.

### 5. Maintainability remains a secondary risk

`src/pocketOS/pocketOS.c` remains relatively monolithic. The existing roadmap correctly treats module splitting as maintainability work rather than an immediate safety prerequisite.

Do not refactor it merely to make the repository look cleaner. Lock behavior with tests first, then split along stable boundaries only where doing so reduces future defect/review risk.

## Maturity snapshot

This is an engineering assessment, not a formal score or release certification.

| Area | Assessment |
| --- | --- |
| Core product concept | coherent and useful |
| UI/product direction | relatively mature for current scope |
| General implementation | substantive, not a mockup |
| Automated regression testing | good foundation |
| Device-aware engineering | meaningful and improving |
| Destructive-operation safety | materially improved on current `main` |
| Release engineering | needs stronger source/artifact/gate binding |
| Evidence provenance | needs tightening |
| Public-release readiness | not yet established |

## Recommended project posture

Until the release-confidence roadmap is complete:

1. keep public distribution paused;
2. avoid major feature expansion;
3. prioritize proof and release discipline over new surface area;
4. preserve current safety behavior with regression tests before architecture cleanup;
5. require a fresh release candidate built from an exact reviewed commit rather than reviving an old package.

The target operating flow is:

```text
code
-> automated tests
-> high-risk behavior verification
-> real-device evidence where required
-> independent review with durable provenance
-> exact commit freeze
-> package/source identity verification
-> explicit release approval
-> publish
```

See [`roadmap-release-confidence-v1.md`](roadmap-release-confidence-v1.md) for the implementation plan.
