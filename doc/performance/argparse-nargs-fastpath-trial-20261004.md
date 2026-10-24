# argparse nargs `_sre` fast-path trial (2026-10-04)

## Result

Rejected. A native shortcut for the exact `argparse` patterns `([A])` and
`(-*A-*)` did not improve the official XLang3 `subparsers` workload. The
implementation and its temporary semantic fixture were removed; the raw
order-balanced samples remain at
`doc/performance/data/argparse-nargs-fastpath-20261004.json`.

## Measurement

The source was `benchmarks/cases/subparsers.py`, run as an order-balanced
control/candidate comparison using Python 3.14's environment. There were 21
paired samples, each measured in both AB and BA order, after three warmups.

| Build | Median |
| --- | ---: |
| Control | 174.108 ms |
| Candidate | 174.328 ms |
| Candidate / control | 1.0037x (95% CI 0.9981–1.0068) |

The interval includes no change and the point estimate is slightly slower, so
there is no performance evidence to keep this specialized matcher. The exact
match/capture/span fixture was run successfully against the candidate before
removing the implementation.

The control executable SHA-256 was
`2ae30fa9a56b11e9bf3298d5a48ca33357c9ff93c097fd29a8b9d485769a1e63`; its
runtime DLL SHA-256 was
`d72db9a593d86d5c793035a4a3fad377d9eee2f60f3ae695f09192deb64b9ca8`. The
candidate had the same executable hash and runtime DLL
`9b234b0bb07149eeb0844a9ff181cd58bda1d79316bd19973e9e24ed875f7656`.
