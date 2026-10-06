# IR `Move` last-use ownership transfer trial (2026-10-06)

## Result

Rejected. I changed the VM's IR `Move` operation to transfer an owned source
value when register metadata said that instruction was its final read. The
source register was left on the original copy path otherwise. The complete
fixture suite passed with the candidate, but official pyperformance did not
show a supported gain on the two hot workloads selected for screening:

| Benchmark | Control | Candidate | Result |
| --- | ---: | ---: | --- |
| `async_tree_none` | 4.43 s ± 0.03 s | 4.46 s ± 0.04 s | 1.01× slower |
| `pickle_pure_python` | 5.30 ms ± 0.48 ms | 5.20 ms ± 0.07 ms | Not significant |

The Pickler profile recorded about 50,200 `Move` operations across 60 dumps,
which made this a plausible general ownership optimization. The official
benchmark did not validate that hypothesis. Do not repeat a standalone
last-use transfer in the generic IR `Move` handler without new evidence that
it removes meaningful work from a timed workload.

## Method and validation

Both Release builds ran pyperformance **1.14.0** in fast mode, with the same
CPython **3.14.7** dependency site and compatibility hooks. The
`async_tree_none` case used a 600-second full-case timeout override. Pyperf
warned that the `pickle_pure_python` fast samples might be unstable and hid
the pair as statistically insignificant. The complete fixture suite passed
on the candidate. No fixed Release gate was run because the focused benchmark
did not establish an improvement; the candidate source change was removed.

The control executable and runtime DLL SHA-256 values were
`0e9468638afd21f9f40d68a27b4bac30f4f8bd96c3534eabfc01a136b5b65179` and
`4a5c42f8611415db0314de9fdaa06ff58fa0cecfcfc60b9da27fa463132343fe`. The
candidate executable and runtime DLL hashes were
`1f4f9e3c7c5f9aba879ae4274f0b953e30883e26bb547c33712a786610c53369` and
`9bb5ccad3cadae3b608c719c268d11635866f89ebea33bacc4de62f3cf14081`.

Raw control and candidate JSON samples and runner logs are preserved in the
[`data` directory](data/), with the `move-op-last-use-*-20261006` prefix. This
trial did not modify any Python standard-library implementation.
