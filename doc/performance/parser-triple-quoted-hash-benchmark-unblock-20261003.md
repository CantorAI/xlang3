# Triple-quoted `#` parsing regression found during pyperformance rerun

The Python 3.14.7 pyperformance workers for `tornado_http` and `docutils` had
been stopping during import with unterminated-string parse errors. The shared
cause was `trim_inline_comment_for_join()` in the lexer: when joining a
parenthesized expression, it recognized single-quoted strings but treated a
`#` inside a triple-quoted raw regex as the start of a comment. This truncated
the literal before the main lexer saw it.

The lexer now recognizes matching triple-quote delimiters (including escaped
delimiters) while trimming joined lines. `tests/cpp/parser_tests.cpp` includes
a regression case containing `#` and both quote characters inside a raw
triple-quoted regex, followed by a real inline comment.

Validation on a scratch Release build: `xlang3_parser_tests.exe` passed, and
imports of `tornado.escape` and `docutils.utils.smartquotes` succeeded. The
fixed Release executable and DLL were not modified. The pyperformance workers
then got past the original parser failure but still did not produce benchmark
scores:

| Benchmark | Next failure after parser fix |
| --- | --- |
| `tornado_http` | XLang3 `asyncio` lacks `WindowsSelectorEventLoopPolicy` |
| `dask` | `psutil._psutil_windows` lacks `virtual_mem` |
| `docutils` | `docutils.parsers.rst.states` raises `KeyError: 'parens'` |

The worker logs are preserved in `doc/performance/data/` with the
`parser-triplehash-` prefix. These are compatibility failures, not performance
measurements; no speedup is claimed from them. Continue from these concrete
runtime gaps and rerun affected benchmarks after fixing them.
