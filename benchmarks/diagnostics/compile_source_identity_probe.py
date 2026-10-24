"""Compile source/AST identity checks; run against CPython before a candidate.

This is a correctness diagnostic, not a performance benchmark. It exercises
the generic source dispatch used by Chameleon's Token(str) compilation.
"""
import _ast
import ast
import sys


class Token(str):
    def __str__(self):
        raise AssertionError("compile must read the original string payload")


class ByteToken(bytes):
    def __str__(self):
        raise AssertionError("compile must decode the original bytes payload")


class MutableToken(bytearray):
    def __str__(self):
        raise AssertionError("compile must decode the original bytearray payload")


def check_source(source):
    assert eval(compile(source, "<identity>", "eval")) == 42
    assert eval(compile(source=source, filename="<identity>", mode="eval")) == 42
    tree = compile(source, "<identity>", "eval", ast.PyCF_ONLY_AST)
    assert isinstance(tree, ast.Expression)
    assert eval(compile(tree, "<identity>", "eval")) == 42


def check_encoded_bytes():
    source = ByteToken(b"# coding: latin-1\nanswer = 'caf\xe9'")
    namespace = {}
    exec(compile(source, "<encoded>", "exec"), namespace)
    assert namespace["answer"] == "caf\u00e9"


def check_fake_sources():
    # A class name and native-looking payload attribute prove no type identity.
    class str:
        __xlang3_string_value__ = "6 * 7"

    class bytes:
        __xlang3_bytes_value__ = b"6 * 7"

    class Expression:
        body = ast.Constant(42)
        _fields = ("body",)

    for source in (str(), bytes(), Expression()):
        try:
            compile(source, "<fake>", "eval")
        except TypeError:
            continue
        raise AssertionError("compile accepted an unrelated source object")


def check_ast_identity():
    tree = ast.fix_missing_locations(ast.Expression(ast.Constant(42)))
    original_ast, original_native = ast.AST, _ast.AST
    try:
        ast.AST = object
        _ast.AST = object
        assert eval(compile(tree, "<rebound>", "eval")) == 42
    finally:
        ast.AST, _ast.AST = original_ast, original_native


def check_ast_subclass():
    class DerivedExpression(ast.Expression):
        pass

    tree = ast.fix_missing_locations(DerivedExpression(ast.Constant(42)))
    assert eval(compile(tree, "<derived>", "eval")) == 42


failures = 0
for name, check, arguments in (
    ("native-str", check_source, ("6 * 7",)),
    ("str-subclass", check_source, (Token("6 * 7"),)),
    ("bytes-subclass", check_source, (ByteToken(b"6 * 7"),)),
    ("bytearray-subclass", check_source, (MutableToken(b"6 * 7"),)),
    ("bytes-source-encoding", check_encoded_bytes, ()),
    ("fake-source-identities", check_fake_sources, ()),
    ("ast-module-rebinding", check_ast_identity, ()),
    ("ast-root-subclass", check_ast_subclass, ()),
):
    try:
        check(*arguments)
    except Exception as error:
        failures += 1
        print("FAIL", name, type(error).__name__, str(error))
    else:
        print("PASS", name)
sys.exit(1 if failures else 0)
