# Tomli and repeated Unicode indexing

The all-97 run of checkpoint `003b3882` is still active. This is a source
audit of a likely asymptotic slowdown, not a candidate timing result. No string
indexing change has been implemented or compiled yet.

The official `bm_tomli_loads/data/tomli-bench-data.toml` input has 16,824,157
UTF-8 bytes, 16,824,037 Python characters, and 68 non-ASCII characters. These
counts were read from the installed pyperformance 1.14.0 input with CPython
3.14.7, outside benchmark timing. The installed shared dependency is Tomli
2.0.1: its wheel metadata says `Root-Is-Purelib: true` and `py3-none-any`.
Its parser is `_parser.py`; there is no native parser extension in this
dependency site. The saved CPython reference report documents the same site.

Tomli repeatedly evaluates `src[pos]` and short slices of that source.
XLang3's `StringObject` already caches whether the complete string is ASCII,
so pure ASCII scalar indexing selects a byte directly. Once any non-ASCII
character occurs, however, both the generic sequence path and the specialized
VM path call `utf8_codepoint_count` over the **whole input** on each indexed
access, followed by `utf8_codepoint_at`, which walks from the beginning to the
requested character. One rare non-ASCII character therefore makes otherwise
ASCII indexing proportional to the source length. Repeating it across the
input can produce quadratic work.

Relevant paths:

- `src/runtime/sequence.cpp`: native string scalar indexing/slicing and length.
- `src/executor/xlang_vm/ops/xlang_vm_ops_containers.h`: cached and uncached
  string integer indexing, plus native string length paths.
- `src/internal/xlang3/value.h`: `StringObject`, `utf8_codepoint_count`,
  `utf8_byte_offset`, and `utf8_codepoint_at`.

The fix belongs in generic immutable-string metadata and accessors, keeping
Tomli's Python parser intact. A cached character count alone is insufficient:
the offset lookup must also avoid walking from byte zero. A lazy index should
retain the existing direct ASCII path and avoid a large per-character table
for inputs containing only a few multibyte characters. Sparse cumulative
UTF-8 corrections or bounded checkpoints are possible representations; choose
using measurements rather than assuming a layout win.

Before implementation, preserve the validated control and record scaling for
repeated scalar reads, `len`, and short slices on ASCII, rare multibyte, and
dense multibyte strings. Construction must stay outside the timed region.
Check negative indices, slice steps, supplementary characters, surrogates,
subclasses, repeated accesses, and string lifetime across threads. Cache
publication must be safe for concurrent readers; every construction or byte
write must establish valid metadata, and destruction must release any index.
The current string destructor is called by `release_string_block`; strings
are not currently recycled through a separate retained-object pool.

Retain a candidate only after the complete correctness suites, unchanged
fixed Release regression gate, and official Tomli benchmark establish its
effect. Record the input size, binary hashes, samples, and timeout outcomes.
A scaling diagnostic does not establish an official suite score or a win
against CPython by itself.
