# Cross-activation exact-function call-cache trial (2026-10-02)

## Hypothesis and implementation

XLang3 clears generic `CALL` site state when a Python frame returns because its
cached callee `Value` can keep a closure and its captured object graph alive.
The trial replaced that strong identity guard with a process-unique scalar tag
on each `FunctionObject`. On a later activation, the VM could compare the live
callee's tag and enter through the normal Python-function frame path without
retaining or dereferencing a stale function pointer. A fixture confirmed
correct dispatch after alternating targets and confirmed that a warmed
temporary closure remained collectible.

This was a VM/runtime change only. It did not alter `pickle.py`, `argparse.py`,
or another standard-library implementation.

## Results and decision

The targeted screen did not establish a material, repeatable speedup:

- A seven-pair direct run of the unchanged pure-Python pickle benchmark body
  had a slightly faster candidate median (1.0882 s vs 1.1019 s across ten
  outer loops), but the opposite-order paired samples varied in direction.
  This stubbed direct runner is diagnostic, not official pyperf.
- The `subparsers` direct A/B was mixed and slightly favored the control by
  median (258.7 ms vs 263.6 ms).
- In the fixed microbenchmark's `function_calls` case, the first candidate was
  5.5% slower than control. Moving the tag check off the same-activation hot
  path reduced the slowdown to 2.6%, but still did not show a gain. The
  harness's `pass` status means only that the slowdown stayed under its 10%
  regression limit; it is not a speedup result.

The runtime change and its fixture were removed. The full official pyperf A/B
could not be repeated because `pyperformance` is absent from the local Python
environments and network access blocks installing it. The evidence below is
retained so this exact cache-lifetime design is not repeated without a new
measurement hypothesis.

After removing the candidate, the normal Release build passed the complete
11-case fixed-baseline gate against `baseline-0336992` with 21 paired samples
per case. The restored executable/runtime hashes are
`4D1317C5940CD607D83519BD54F74E634E26DA4F42D3EFFAD8CC53A68985A868` and
`888F4DC2D64EC4E84E01204DC7058967EE8FF67D12EF087BF9D636772681CD63`.
See the complete [rollback gate report](data/call-cache-revert-fixed-gate-20261002.json).

## Preserved evidence

- [Pure-Python pickle direct A/B](data/call-cache-cross-activation-pickle-ab-20261002.csv)
- [`subparsers` direct A/B](data/call-cache-cross-activation-subparsers-ab-20261002.csv)
- [Initial `function_calls` paired gate](data/call-cache-cross-activation-function-calls-20261002.json)
- [Refined `function_calls` paired gate](data/call-cache-cross-activation-function-calls-r2-20261002.json)

Control binary identities: executable
`C3D48136EE8E94A89B0DBEF1EC2C433164E86D366C78EA0FA3F07AF9ABE8D938`, runtime
`A4F748F13B4E7219A3E1D11F5FF4CB366DEE66ADD2D416B4A1954DA4CD1C812C`.
First candidate identities: executable
`960DF323B5718DF86D1DB206BD22FB4D7C354C28E25246A822BF487F81D90B09`, runtime
`0A0E536F2BF4F5FABE8C1AB1AB77B7C2862964CD74310F7C6464FCEBB535E706`.
The refined candidate retained the same executable and had runtime hash
`E642157CC4050B6897AF74EABBA3BF2A73AF54FAA25CAC79846D7F50915CECA9`.
