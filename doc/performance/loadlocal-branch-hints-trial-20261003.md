# Local-load branch hints trial (2026-10-03)

## Result

Marking the common valid-index and initialized-local branches unlikely in
`load_local` and `load_local_pair` did not establish a speedup. The candidate
preserved identical program output and measured **0.9970x** of control time on
`subparsers` (95% interval **0.9805-1.0074**) and **0.9956x** on `local_slots`
(95% interval **0.9443-1.0471**). Both intervals cross parity. The candidate
was reverted.

Each result uses 21 paired measurements, testing both AB and BA process order
for every pair, after five warmups. Both binaries were built from the same
current source snapshot with MSVC Release `/O2 /Ob3`; the only source difference
was the branch prediction annotation on local bounds and unbound checks. The
runs used Python 3.14.7 and compared output on every launch.

| Build | Executable SHA-256 | Runtime DLL SHA-256 |
|---|---|---|
| Control | `15BFE25B70225DFC95E895ADA88A9D8894FBB85D2474137561FF41476CB611B6` | `E8FF6560C697EBE85F67DA6EDEA6AD76C994D5954F066DCAF1A4C3BFC7D6917` |
| Candidate | `08FD0CF0D74BEF993B182328A5AB6D4A44CE67FB62EF9772338AE89E1352F33B` | `595FDDB179C93A666BBCDD6FFB9D08EC1FC2AC8C1D39A7F9041498ABEEAD7E59` |

The fixed `build-repro/Release` executable and DLL remained unchanged. The
trial is too small and noisy to address the full-suite gap; subsequent work
should stay focused on high-cost library workloads and call paths.

## Raw measurements

- [`subparsers` paired samples](data/loadlocal-branch-hints-subparsers-20261003.json)
- [`local_slots` paired samples](data/loadlocal-branch-hints-local-slots-20261003.json)
