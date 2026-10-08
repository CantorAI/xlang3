# Official affected workload: pyflate

Source-only finding. No import, AST parse, runtime, benchmark, build, or live source edit was performed for this review.

The original pyperformance `pyflate` definition exercises S8's exact-Int64 sorting branch inside its timed decompression body. In `C:/Python/Python314/Lib/site-packages/pyperformance/data-files/benchmarks/bm_pyflate/run_benchmark.py`, `bench_pyflake` starts the timer at line 635 before the per-iteration decoder. Its BZip2 branch calls `bzip2_main` at line 645. The decoder calls `bwt_reverse(b"".join(buffer), pointer)` at line 446; `bwt_reverse` calls `bwt_transform`, whose line 294 is `F = bytes(sorted(L))`. Thus each BZip2 block sorts an exact bytes sequence during timed work, without a key callback. The run uses the unchanged `data/interpreter.tar.bz2` file and checks the decompressed MD5 `afa004a630fe072901b1d9628b960974` after measurement (line 653).

XLang3's bytes sequence iterator calls `sequence_get_item` (`src/runtime/sequence.cpp` lines 878–880), and its exact bytes item branch publishes `ValueTag::Int64` via `value_set_int64` at line 1303. With no explicit key, `collect_sorted_entries` uses each item as its key. Consequently the complete key set satisfies S8's Int64 proof in `sort_entries`; this is an actual timed-body admission, independent of setup-only sorting of benchmark names.

Pinned original source SHA-256: `ec5347c7045af86ba33e1b2c6f64bc4b506d0ade3b77391d7e41863fe25fb464`.

Pinned compressed input SHA-256: `81101162ee7fc7a3db86d1a87e0c86781304eb9026aca4078432004a1383c51a`.

The latest completed all-97 evidence (`doc/performance/data/pyperformance-xlang3-full-refresh-vs-cpython3147-fast-20261008-subtests.csv`) records saved CPython 3.14.7 mean 0.34492854 s and historical XLang3 mean 2.97763217 s for `pyflate`. Those results are unpaired and precede S8. They do not establish a current improvement or a CPython win. A fresh original `pyflate` official attempt after the current full validation terminates is the suitable affected-workload check; keep the decoder, input, checksum, and pyperformance definition unchanged.

Other direct suite sorting sites are unsuitable for this admission: benchmark-name lists are setup-only, `mdp` supplies tuple keys, `comprehensions` sorts tuples, and `pyflate`'s Huffman-table list sort supplies object keys. None was substituted for the bytes sort above.
