# Mako import failure: dynamically populated integer-key dictionary

In the full XLang3/CPython 3.14.7 run
`pyperformance-xlang3-current-candidate-full-fast-20261010-r1`, benchmark 52
(`mako`) died while importing Pygments. `pygments.lexers.lisp` builds
`number_rules` by assigning entries for bases `(2, 8, 10, 16)` and then reads
`number_rules[2]`; XLang raised `KeyError: '2'` at that lookup.

The existing `class_body_for_loop_namespace.py` fixture already covers the
same integer-key construction and lookup inside a class body. Static source
inspection shows that `lower_class_body_stmt` has no `ForStmt` case, explaining
why the class body reaches the lookup with an empty dictionary. Do not change
the Pygments or Mako Python package code. Once the baseline run finishes, run
the fixture on CPython 3.14.7 and XLang3 and implement the necessary class
namespace semantics.
