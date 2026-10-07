# Tomli and repeated Unicode indexing

The all-97 run of checkpoint `003b3882` has finished: Tomli timed out at the
unchanged 300-second cap. A first candidate implementing shared Unicode
indexing, length, and slices passed correctness and the fixed performance gate,
but official Tomli still timed out. Search and regex conversion fixes were
subsequently validated. With the longer matching 900-second cap, the final
candidate now has a complete official score; CPython is still faster. See the
completed outcome below and the checkpoint report for the raw evidence.

The official `bm_tomli_loads/data/tomli-bench-data.toml` input has 16,824,157
UTF-8 bytes, 16,824,037 Python characters, and 68 non-ASCII characters. These
counts were read from the installed pyperformance 1.14.0 input with CPython
3.14.7, outside benchmark timing. The installed shared dependency is Tomli
2.0.1: its wheel metadata says `Root-Is-Purelib: true` and `py3-none-any`.
Its parser is `_parser.py`; there is no native parser extension in this
dependency site. The saved CPython reference report documents the same site.

Tomli repeatedly evaluates `src[pos]` and short slices of that source.
The preserved control's `StringObject` already caches whether the complete string is ASCII,
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

The preserved control's non-ASCII `utf8_slice_text` helper also builds a temporary
`vector<size_t>` containing offsets for the entire source on **each slice**,
reserving `storage.size() + 1` elements. On this input and 64-bit Windows,
that reservation is about 134.6 MB, even when requesting two characters.
Avoiding scalar rescans alone would leave this repeated whole-source
allocation intact. Shared immutable offset metadata must serve short slices
too, while retaining correct stepped and reversed slices.

The validated control was preserved before implementation. Record scaling for
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

`benchmarks/diagnostics/native_unicode_index_scaling_probe.py` prepares fixed
256-operation measurements on 256-, 4,096-, and 65,536-character inputs.
It separates first-character reads, last-character reads, length, and short
tail slices for ASCII, sparse multibyte, and dense multibyte text. Construction,
content assertions, and an unmeasured warmup stay outside timing; five raw
samples and a checksum are retained per row. The official benchmark must
still account for building any new index. CPython 3.14.7, the preserved control,
and the first candidate each completed all 36 diagnostic rows with matching
checksums. Raw evidence is in `data/native-unicode-index-scaling-*-20261007.json`.
The large improvements are relative to old XLang3; they are not CPython wins.


## Candidate design and remaining search conversions

The first candidate keeps the StringObject header size and UTF-8 payload offset
unchanged. Its existing flags/padding word now holds ASCII/immortal flags and
an atomically published pointer to immutable Unicode metadata. Exact ASCII
strings retain direct byte positions without a Unicode allocation. Non-ASCII
strings up to 64 bytes retain short scans. Larger strings cache their character
count and choose either at most 128 sparse cumulative byte corrections or
checkpoints every 128 characters for dense Unicode. Destroying the string frees
the index. Readers race only to publish complete immutable metadata; a losing
builder frees its own allocation. Shared offsets serve scalar accesses and
short slices instead of rebuilding an offsets vector for every slice.

The first candidate passed the complete fixture suite, all eight selected
C++/SDK/serialization checks, and the full default fixed-baseline gate (11
cases, 21 paired repeats, five warmups, unchanged 10% tolerance, exit 0). Tests
also found and fixed existing zero-step slice exception and string-subclass
`__len__` dispatch mistakes. Official fast-mode Tomli still timed out at 300
seconds; its unchanged start/end binary hashes are recorded in
`data/pyperformance-xlang3-native-unicode-index-tomli-fast-20261007-provenance.json`.

Tomli's Python `skip_until` calls `src.index(expect, pos)`. The native string
search bounds helper still scanned the complete source for its length and
walked from byte zero to convert start/end positions. A successful search then
rescanned the prefix to convert its byte result into a Python character index.
This happened even for ASCII. The new search scaling diagnostic measures 64
operations on 256-, 4,096-, and 65,536-character sources, with five raw samples,
construction and warmup outside timing, and a checked result checksum. On the
65,536-character ASCII source, the first candidate takes about 19.65 ms for
`find` at the tail and 19.80 ms for `rfind`; CPython takes about 0.004 ms.
These diagnostic figures establish repeated conversion work, not an official
benchmark result.

The next candidate routes search bounds through the shared count/offset cache
and converts result byte positions with its inverse lookup: sparse correction
binary search or a nearby dense checkpoint plus a bounded prefix scan. Empty
substring counting uses the two cached character positions. Empty-needle and
prefix/suffix bounds must still reject starts past the end and reversed bounds,
as CPython does. The Python Tomli parser remains unchanged. This candidate
passed a fresh complete correctness run and fixed gate; its official Tomli
run still timed out, as recorded below.


## Regex matching also copied and rescanned Unicode subjects

The second candidate passed complete correctness and the unchanged full fixed
gate, and removed the native search scans in its diagnostic. Official Tomli
still timed out at 300 seconds, with unchanged binary hashes. That outcome is
recorded in `data/pyperformance-xlang3-native-unicode-search-tomli-fast-20261007-*`.

Tomli's numeric values try `RE_DATETIME.match`, `RE_LOCALTIME.match`, then
`RE_NUMBER.match` at advancing positions. In `sre_module.cpp`, the existing
immutable-subject path was restricted to ASCII. A rare Unicode character
forced the fallback to copy the entire source, recount its characters, walk
to each bound, rescan prefixes for captured spans, and copy the source again
into successful matches. Group extraction walked those prefixes again.

`native_unicode_regex_scaling_probe.py` isolates anchored tail matching and
capture extraction on the same three input families. Before changing regex,
64 matches took 27.918 ms on sparse Unicode and 42.284 ms on dense Unicode at
65,536 characters. The third candidate extends the existing in-place path to
exact immutable Unicode strings, retains the original subject in the Match,
and uses the shared cache for bounds, spans, repeated-capture repair, group
extraction, and expansion. Mutable buffers, subclasses, and deferred
lookbehinds retain their fallback. Text and bytes pattern types must still
match. Tomli remains Python; this changes XLang3's existing native `_sre`.

The third candidate diagnostic takes 0.247 ms and 0.320 ms respectively, with
matching checksums. CPython is still about 9–12 times faster in those rows.
Complete fixtures (including capture spans, empty/repeated groups, source
lifetime, expansion, and mixed-type rejection), all eight C++/SDK/serialization
checks, and the full default gate pass. The third official Tomli run completed
calibration and three measured workers, then timed out during full collection
at the unchanged 300-second cap. It produced no final score. Start/end hashes
match in `data/pyperformance-xlang3-native-unicode-regex-tomli-fast-20261007-provenance.json`.

A fresh CPython 3.14.7/XLang3 pair completed sequentially with a matching
900-second complete-case allowance, preserving fast mode, workload, values,
and warmups. This is a longer deadline, not a shortened benchmark. The previous
300-second timeout remains a failure in its original evidence; the new completion
with the longer deadline must not be counted as a 300-second completion or
used to rewrite the frozen all-97 report. No official score or speedup is
claimed from diagnostic timings.

The completed official pair has 20 values each: CPython mean 1.870011 s,
XLang3 mean 22.254785 s. CP/X speed is 0.08403x; CPython remains
about 11.90x faster. Both binaries retain identical start/end hashes.
See [the checkpoint report](native-unicode-index-search-regex-20261007.md) for
raw files, charts, validation, and the limits of this comparison.
