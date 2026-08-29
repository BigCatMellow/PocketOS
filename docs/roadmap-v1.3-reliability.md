# PocketOS v1.3 Reliability Roadmap

State: WORKING

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
- [ ] Redesign ambiguous/CD/arcade ZIP importing.
- [ ] Replace executable Python overrides with data-only overrides.
- [ ] Stream ZIP extraction with size/free-space/collision limits.
- [ ] Add transactional install/uninstall recovery tests.
- [ ] Run real-device stress validation.
- [ ] Split launcher modules only after behavior is locked by tests.
