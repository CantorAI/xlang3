# Triple-string closing-comment parser checkpoint

The lexer now removes a real comment after a multiline triple-quoted string closes inside a continued call. It keeps hashes inside the string, literal newlines, raw-string behavior and escaped delimiters. SQLAlchemy's unchanged `selectable.py` now parses without errors. The library remains Python.

Before the fix, line joining retained the closing physical line's `# noqa` suffix. Tokenization then treated the rest of the joined logical line as that comment and lost later call delimiters. Four minimized inputs were accepted by CPython 3.14.7; the previous XLang3 accepted both no-comment controls and rejected both closing-comment variants. The misleading `of` keyword hint was secondary error reporting, not the grammar defect. The earlier October 3 interior-hash correction addressed a different case.

The two owned changes are `src/parser/lexer.cpp` and `tests/cpp/parser_tests.cpp`. Sixteen new C++ semantic cases check exact literal contents and call arguments across both quote styles, raw strings, interior hashes, escaped delimiters and malformed input. All ten fresh focused phases passed, including those C++ tests, all four original inputs through XLang3 and the direct native parser, and the complete unchanged installed SQLAlchemy source.

Fresh correctness passed: 399 core fixtures, 11 compatibility sections, three expected failure cases, nine selected CTests and two native SQLite API checks. The unchanged default 11-case regression gate passed with 21 repeats, five warmups and a 10% threshold against the fixed accepted baseline. The CTest watcher retained its raw invalid timing flag for the owned CTest process; that phase was untimed, passed its semantic checks, and never accepted timing. All timed gate and official attempts had valid strict watchers and stable hashes.

Both original SQLAlchemy benchmark attempts on XLang3 now pass parsing and fail later at `table.c` in `sqlalchemy/sql/schema.py:3852` with `RuntimeError: property getter is not callable`. Neither produced a complete score. CPython 3.14.7 completed 20 values for each original benchmark. The terminal validation receipt remains `trial_validation_failed`, with correctness passed and `full_validated=false`. This is a parser correctness checkpoint; it establishes no SQL speedup or CPython win.

| Original official benchmark | XLang3 result | Fresh CPython 3.14.7 result |
| --- | --- | --- |
| SQLAlchemy declarative | Later property getter failure; no score | 20 values; mean 0.0923821475 s |
| SQLAlchemy imperative | Later property getter failure; no score | 20 values; mean 0.0111890175 s |

All 40 CP values and all gate arrays are exported without trimming. Original warnings, failed XLang3 streams, empty/missing output identities and raw watcher observations are retained. These fast-mode CP measurements are unpaired; no XLang3/CPython ratio can be computed from failed XLang3 cases.

The measured candidate is the exact source124 worktree and Release178 identified by the applied-source, build, focused and validation receipts. Only the two owned parser changes are included in this checkpoint. Other dirty sources are preserved and excluded. The previous published UTF8 source119 archive supplies unchanged source bytes; this package adds only the five parent parser paths and the two current changed files. This subset and the binary manifests describe the measured build, not a clean-main reproduction guarantee. No runtime binaries or objects are published.

The separate post-UTF8 native sampling and COFF attribution archive predates this lexer change. It is diagnostic evidence for its own exact source119/Release identity, including unresolved ranges and opaque object limits. The initial busy-process refusal, which launched no child, is preserved separately from the completed capture. Its samples are not CPU shares and do not attribute costs to this parser candidate. The earlier 97-case comparison remains an older run; this checkpoint did not rerun the whole suite.

Evidence: [terminal validation](data/triple-string-closing-comment-validation-20261008.json), [applied source](data/triple-string-closing-comment-applied-source-20261008.json), [fresh focus](data/triple-string-closing-comment-focused-20261008.json), [original reproducer](data/sqlalchemy-triple-closing-comment-reproducer-20261008.json), [complete CP values](data/triple-string-closing-comment-official-cpython-values-20261008.csv), [gate summary](data/triple-string-closing-comment-fixed-gate-summary-20261008.csv), [all gate arrays](data/triple-string-closing-comment-fixed-gate-values-20261008.csv), [publication manifest](data/triple-string-closing-comment-checkpoint-publication-20261008.json).
