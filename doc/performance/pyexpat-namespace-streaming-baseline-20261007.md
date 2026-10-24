# Native XML parser correctness baseline — 2026-10-07

CPython **3.14.7** and the validated XLang3 **07674515** engine disagree on all four bounded event probes. These are correctness observations, not timing scores.

| Probe | CPython events | XLang3 events | Difference |
|---|---:|---:|---|
| Namespace document, one final chunk | 8 | 4 | Namespace callbacks absent; names and attributes remain unexpanded |
| Namespace document followed by final empty chunk | 8 | 8 | Missing namespace events plus replayed element events |
| Plain document followed by final empty chunk | 4 | 8 | Complete document callbacks emitted twice |
| Plain document split inside a tag | 4 | 9 | Previously delivered root events replayed across input chunks |

For `<r xmlns="urn:default" xmlns:p="urn:prefix" p:a="v"><p:c/></r>` with separator `|`, CPython emits start/end namespace declarations, element names `urn:default|r` and `urn:prefix|c`, and attribute `urn:prefix|a`. XLang3 instead emits `r`, `p:c`, and the raw `xmlns`, `xmlns:p`, and `p:a` attributes.

Source review matches these observations: `namespace_separator` is stored by `ParserCreate` but unused when emitting element/attribute names. Each `Parse` appends input to `buffer`, then `parse_document` starts at offset zero, clears the element stack and resets positions. Complete earlier events are replayed, including when a final empty chunk is supplied.

These defects are consistent with the previously recorded invalid `genshi_xml` output: the template remained unexpanded and was rendered twice. That source-backed inference must be verified by exact output after a fix; the historical apparent 50.9× timing win remains excluded.

The next change must preserve parser state across input chunks and implement namespace expansion/callback scope in XLang3's own native `pyexpat` counterpart. Genshi and Python XML library algorithms must remain Python. Compare callback order, final-empty input, split tokens, namespace shadowing, default namespace versus attribute rules, and exact Genshi output before accepting any timing score.

[Probe source](../../benchmarks/diagnostics/pyexpat_namespace_streaming.py), [raw CPython/XLang3 events and hashes](data/pyexpat-namespace-streaming-baseline-20261007.json), [previous Genshi output evidence](data/genshi-render-correctness-20261007.json).
