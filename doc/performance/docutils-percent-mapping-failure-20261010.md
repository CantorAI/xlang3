# docutils import failure: mapping-based percent formatting

In the full XLang3/CPython 3.14.7 run
`pyperformance-xlang3-current-candidate-full-fast-20261010-r1`, benchmark 37
(`docutils`) failed before timing. The unchanged `docutils.parsers.rst.states`
source populates `pats` by iterating `enum.formatinfo.keys()`, then formats a
template containing `%(parens)s` with that dictionary. XLang raised
`KeyError: "format mapping key 'parens' not found"` at the template.

The compiler source narrows the cause: `lower_class_body_stmt` has no
`ForStmt` case, while ordinary statement lowering does. The loop therefore
does not populate `pats` in this class body; `%` formatting is only where the
missing class-scope store becomes visible. The existing
`class_body_for_loop_namespace.py` regression fixture now also covers the
`dict_keys` loop and mapping-format pattern. The separate
`dict_keys_mapping_format.py` fixture checks those mapping operations outside
a class to distinguish the compiler scope bug from dict or formatting bugs.

Validation is pending until the active full-suite run releases the machine;
the run uses the pre-change fixed Release executable. Afterward, run the
fixture under XLang3 and CPython 3.14.7, then narrow the failure to the first
divergent intermediate value before changing runtime code.
