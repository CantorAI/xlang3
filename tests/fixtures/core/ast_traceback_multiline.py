import ast
import traceback


source = "_run_module_code(code, init_globals, run_name,\n                        pkg_name=pkg_name, script_name=path_name)"
call = ast.parse(source).body[0].value
print((call.lineno, call.col_offset, call.end_lineno, call.end_col_offset))
print((call.func.lineno, call.func.col_offset, call.func.end_lineno, call.func.end_col_offset))


def fail(first, second):
    raise ValueError("multiline")


try:
    fail(
        1,
        2,
    )
except ValueError as exc:
    rendered = "".join(traceback.format_exception(exc))
    print("ValueError: multiline" in rendered, "fail(" in rendered)
