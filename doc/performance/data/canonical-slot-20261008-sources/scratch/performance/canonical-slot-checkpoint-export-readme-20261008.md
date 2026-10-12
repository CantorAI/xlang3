# Canonical slot checkpoint exporter

Prepared tooling only. The subagent did not execute the exporter, fixture, benchmark, build, staging or finalizer.

`export-canonical-slot-checkpoint-20261008.py` reads existing terminal records and produces a checkpoint Markdown report, two horizontal SVG charts, separate diagnostic/official sample and summary CSVs, a comparison JSON, and a lossless raw-file archive/manifest. It has no subprocess, runtime, compiler or git invocation.

Run from `D:/CantorAI/xlang3` with `C:/Python/Python314/python.exe`. Default output is `scratch/performance/canonical-slot-report-preview-20261008`. A later parent-run export with `--output-dir doc/performance` uses the repository checkpoint layout. The script refuses changed existing output bytes; use a fresh scratch preview path for a different input snapshot.

Inputs are the original R3 `canonical-slot-paired-20261008.json`, fresh R4 `canonical-slot-r4-paired-20261008.json` and focused correctness record. Each revision retains all 300 samples: 20 separate CPython reference samples and 280 XLang3 samples, all four cases and seven alternating pairs. R3 and R4 CPython observations are never pooled. The original R3 inherited regression is marked rejected; the nominal slower R4 inherited and ordinary controls stay visible. Speed means reference time divided by runtime time: 1× equal, greater than 1× faster.

The official table uses the terminal 20261007 full-fast CPython 3.14.7 and historical XLang3 SQLGlot rows. The historical XLang3 reference is explicitly distinct from the accepted paired control. R4 official scores are added only from the original `pyperformance-canonical-slot-r4-sqlglot-parse-fast-20261008.json` after a successful terminal validation record verifies its output hash, original command, candidate/source identity, eight C++ tests and unchanged fixed gate. The shimmed runner creates no per-run provenance file; the authoritative validation receipt is used. Warmups/calibration and failed partial outputs remain archived but are unscored.

Raw original results/logs, failure evidence, R3 tested source snapshot, R3/R4 scratch proposals/reviews/scripts, current tested R4 sources, original installed SQLGlot benchmark source and both preserved Release manifests are copied without encoding or line-ending changes. The complete input inventory is frozen before any destination writes, the destination archive is excluded from enumeration, and hashes/lengths record every original byte. Generated file hashes are recorded separately. No source edits or acceptance decision occur in the exporter.

Parent must run and inspect the export before committing it. Static review alone does not confirm report rendering or the exporter's runtime behavior. It makes no new 97-case or whole-suite CPython performance claim.
