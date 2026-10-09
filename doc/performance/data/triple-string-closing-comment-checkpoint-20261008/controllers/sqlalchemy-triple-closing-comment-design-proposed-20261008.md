# Continued triple-string closing comments: bounded diagnosis

The root-owned direct-parser capture (`8639e001f802b75759192015962681a8c4b6cbbc5513c0750be556797008b8aa`)
preserves actual errors at SQLAlchemy `selectable.py` lines 893, 923 and 950.
Each follows a multiline `util.symbol(...)` string whose closing line has a
real `# noqa` suffix and whose call-closing `)` occurs on the next line.
The library remains unchanged; this is a parser compatibility diagnosis.

The four independent source files vary only triple-quote style and the real
closing-line comment. Every file retains the same interior `#`, both quote
characters and exact literal newlines. All four must parse, execute and print
their strict PASS label under CPython 3.14.7. No failing XLang3 outcome is an
accepted expectation. Root can capture each independently using the already
compiled direct parser, then run the toy content assertions separately.

Current `src/parser/lexer.cpp:563-567` preserves the entire physical line when
it begins inside a triple string. On the closing line this also preserves the
real suffix comment. The joined logical buffer subsequently presents that
comment before the later `)` token. `trim_inline_comment_for_join` at line37
knows how to close triple strings but currently starts every scan outside one.
The Oct3 triple-hash fix and its one-line raw-regex test preserve interior
hashes; they do not cover this multiline closing-line state transition.

Held correction shape, not an engine patch: give the trim helper initial
`in_string`, `quote` and `triple_string` state by value, with existing false/zero
defaults. Pass the pre-line `continued_string`, `continued_quote` and
`continued_triple` values through `append_joined_line` at both call sites
(current lines785 and816). Always append the physical newline and then the
state-aware trimmed view. Continue calling `update_line_join_state` on the
original physical line with its existing mutable continuation state.

The local trim scan must retain every interior hash until the matching real
delimiter closes, using its existing escaped-delimiter parity rule. Only then
may it trim a real comment; a subsequent quoted suffix must still protect its
own hashes. Preserve literal newlines, both quote styles, raw-string behavior,
existing one-line cases and unterminated-string diagnostics. Test escaped
delimiters and close/reopen suffixes before a later production proposal.
No lexer or SQLAlchemy file was changed, and no execution occurred by author.
