# `print` keyword-name comparison trial (2026-10-02)

`telco` executes `print(t, file=outfil)` 5,000 times per benchmark loop. The
native `print` keyword callback compared each borrowed keyword name by first
constructing a temporary `std::string`. This trial compared the name as a
`std::string_view` instead, without changing keyword validation or print
behavior.

The focused print semantics fixture passed on the candidate. Matched Release
builds, compiled from the same working tree and differing only in this source
change, did not show a pyperformance gain:

| Build | `telco` mean |
|---|---:|
| Control | 263 ms ± 13 ms |
| Candidate | 267 ms ± 14 ms |

`pyperf compare_to` reports the candidate at 1.01× the control time, with no
significant difference. A separate fast-mode comparison against an older
accepted runtime was 1.04× faster, but the matched rigorous A/B did not
reproduce that direction. The source optimization was discarded. It should
not be counted as a performance gain.

The saved rigorous JSON results are
[`control`](data/telco-print-keyword-control-rigorous-20261002.json) and
[`candidate`](data/telco-print-keyword-candidate-rigorous-20261002.json). The
fast screening result is
[`candidate`](data/telco-print-keyword-fast-20261002.json). Runtime DLL SHA-256
identities were `508C2AB373C721B8ACBC9C4EB23F71D8E20FDC375016FB291711920077E28A1B`
for control and
`883C21FD86B130DA71BD84DD868FDF3F95BE24219DF5AEB8E50A3DF45C392D16` for
candidate. Both builds passed the print-focused semantics fixture; after
discarding the trial, the rebuilt Release runtime-value tests, print fixture,
and Decimal arithmetic fixture pass.
