# Direct resume inside the `yield from` trampoline — 2026-10-06

## Question

Once the synchronous `yield from` trampoline validates a complete delegation
chain, it resumes the deepest generator through the public `generator_send`
path. That path immediately attempts to discover a trampoline chain again; the
running-state guard then rejects the duplicate scan and falls through to the
normal resume. This trial used a private resume helper to skip that second
trampoline discovery only for members of the already-validated chain. Ordinary
Python generator sends continued to use the guarded public path.

## Result

The official pyperformance 1.14.0 `generators --rigorous` control measured
**322 ± 25 ms** and the candidate measured **317 ± 21 ms**. `pyperf
compare_to` reported the difference as not significant. The candidate code was
removed; the five-millisecond shift is not strong evidence of a gain.

The complete 11-case Release regression gate passed, as did the fixture suite
and both Release C++ test executables. Those checks establish correctness and
absence of a broad regression, not a generator speedup.

| Artifact | SHA-256 |
| --- | --- |
| Control executable | `7FB9FD272F16D0374596DA1149F61226F3011BB2EC80539F70900CDBB68B357D` |
| Control runtime DLL | `CCC5EC8DDA34F0686799FCBEF0080907B30F27D79F8E44D0C28B9A9EE4C33259` |
| Candidate runtime DLL | `69BC62B923F41BFE7B4041665D00A355751F516657BE7C6E51EE21F66C34CD59` |

The benchmark used CPython 3.14.7 and the official `bm_generators` body. The
same-version full-fast reference measured CPython at **31.4 ms**, leaving the
current XLang3 control about **10.2× slower** on this benchmark. The control
executable and DLL were restored to the unchanged path
`build-repro/main-verify-20261006/Release/` and their hashes verified.

## Evidence

- [Official pyperf control](data/generators-resume-runtime-control-rigorous-20261006.json)
- [Official pyperf candidate](data/generators-direct-resume-candidate-rigorous-20261006.json)
- [Comparison output](data/generators-direct-resume-compare-20261006.txt)
- [11-case Release regression gate](data/generator-chain-direct-resume-fixed-gate-20261006.json)

This optimization did not close the generator gap and is not retained in
source. The next generator trial must remove a larger part of per-yield VM
resume/frame work to justify its semantic complexity.
