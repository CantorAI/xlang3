import ast
import warnings

warnings.simplefilter("error", DeprecationWarning)
alias = ast.alias(
    name="pkg",
    asname=None,
    lineno=3,
    col_offset=4,
    end_lineno=3,
    end_col_offset=7,
)
print(ast.alias._attributes)
print(alias.lineno, alias.col_offset, alias.end_lineno, alias.end_col_offset)
