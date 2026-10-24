# `argparse_subparsers`: terminal-size native fast-call trial (2026-10-02)

The current full pyperformance report measured `argparse_subparsers` at
250.8 ms versus 8.15 ms on CPython 3.14.7, about **30.8× slower**. A fresh
current-Release `--fast` run measured **258 ms ± 17 ms**. The benchmark creates
1,000 optional arguments twice; CPython's Python `argparse.ArgumentParser.add_argument`
creates a `HelpFormatter` for each option, and `HelpFormatter.__init__` calls
`shutil.get_terminal_size`, which queries `sys.__stdout__.fileno()` and
`os.get_terminal_size()` when `COLUMNS` and `LINES` are unset.

An uninstrumented direct workload profile confirmed 1,004 calls each to
`TextIOWrapper.fileno` and `os.get_terminal_size`, alongside 54,360 native
calls, 53,387 `CallMethod` instructions, 46,906 `Call` instructions, and
about 1.07 million VM instructions total. The Python call profile also showed 2,009
`KeyError` constructions from the absent environment variables. The raw
counter report is
[`current-release-argparse-subparsers-vm-counters-20261002.txt`](data/current-release-argparse-subparsers-vm-counters-20261002.txt).

I tried the existing register-backed native callback adapters for
`os.get_terminal_size` and stream `fileno`. The actual `sys.__stdout__` uses
the special class in `sys_module.cpp`, so the `_io.TextIOWrapper` adapter did
not affect its 1,004 calls. The `os.get_terminal_size` adapter did route all
1,004 calls through its fast callback, but the official benchmark regressed:

| Variant | `argparse_subparsers` |
| --- | ---: |
| Before adapter | 258 ms ± 17 ms |
| Adapter candidate | 268 ms ± 13 ms |
| `pyperf compare_to -v` | 1.04× slower, significant |

The raw results are [pre-change](data/current-release-argparse-subparsers-fast-20261002.json)
and [candidate](data/argparse-subparsers-os-fastcall-fast-20261002.json). A
separate run with `COLUMNS=80` and `LINES=24` measured 267 ms ± 15 ms; this
controlled-environment diagnostic shows that avoiding terminal queries does
not remove the dominant slowdown. Its raw result is
[`argparse-subparsers-terminal-size-env-diagnostic-20261002.json`](data/argparse-subparsers-terminal-size-env-diagnostic-20261002.json).

Both source adapters were removed. The broad cost remains in Python method
calls and VM instructions; the next experiment should target those shared
execution paths, not the terminal-size native calls or a C++ replacement for
the pure-Python `argparse` module.
