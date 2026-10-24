import _ast
import ast


class Token(str):
    def __str__(self):
        raise AssertionError("source conversion called __str__")


class GuardedToken(Token):
    def __getattribute__(self, name):
        if name == "__class__":
            return type(self)
        raise AssertionError("source conversion inspected a payload attribute")


class BrokenClassToken(Token):
    def __getattribute__(self, name):
        if name == "__class__":
            raise LookupError("source class lookup")
        return super().__getattribute__(name)


class ByteToken(bytes):
    def __str__(self):
        raise AssertionError("source conversion called __str__")


class MutableToken(bytearray):
    def __str__(self):
        raise AssertionError("source conversion called __str__")


for source in ("6 * 7", Token("6 * 7"), GuardedToken("6 * 7"),
               ByteToken(b"6 * 7"), MutableToken(b"6 * 7"),
               memoryview(b"6 * 7")):
    assert eval(compile(source, "<source>", "eval")) == 42
    assert eval(compile(source=source, filename="<source>", mode="eval")) == 42
    tree = compile(source, "<source>", "eval", ast.PyCF_ONLY_AST)
    assert isinstance(tree, ast.Expression)
    assert eval(compile(tree, "<tree>", "eval")) == 42
print("native sources and subclasses", True)

# Buffer acceptance depends on contiguity and lifetime, not truthiness: a
# valid zero-length view still compiles, while strided/released views do not.
exec(compile(memoryview(b""), "<empty-buffer>", "exec"))
released_source = memoryview(b"6 * 7")
released_source.release()
for source in (released_source, memoryview(b"66**77")[::2]):
    try:
        compile(source, "<invalid-buffer>", "eval")
    except TypeError:
        pass
    else:
        raise AssertionError("compile accepted an unavailable contiguous buffer")

try:
    compile(BrokenClassToken("6 * 7"), "<class-hook>", "eval")
except LookupError as error:
    assert str(error) == "source class lookup"
else:
    raise AssertionError("compile ignored the source's __class__ lookup error")
print("source class lookup exception", True)

namespace = {}
exec(compile(ByteToken(b"# coding: latin-1\nanswer = 'caf\xe9'"),
             "<encoded>", "exec"), namespace)
assert namespace["answer"] == "caf\u00e9"
assert eval(compile(ByteToken(b"\xef\xbb\xbf6 * 7"), "<bom>", "eval")) == 42
for source in ("6\x00 * 7", b"6\x00 * 7", memoryview(b"6\x00 * 7"),
               ByteToken(b"\xef\xbb\xbf# coding: latin-1\n6 * 7")):
    try:
        compile(source, "<invalid>", "eval")
    except SyntaxError:
        pass
    else:
        raise AssertionError("invalid source encoding or embedded NUL accepted")
print("source encoding and NUL", True)


class DerivedExpression(ast.Expression):
    pass


class DerivedConstant(ast.Constant):
    pass


class DerivedBinOp(ast.BinOp):
    pass


class DerivedAdd(ast.Add):
    pass


tree = ast.fix_missing_locations(DerivedExpression(
    DerivedBinOp(DerivedConstant(20), DerivedAdd(), DerivedConstant(22))))
assert eval(compile(tree, "<derived>", "eval")) == 42
names = ((ast, "AST"), (_ast, "AST"), (ast, "Expression"),
         (_ast, "Expression"), (ast, "Constant"), (_ast, "Constant"))
originals = [getattr(module, name) for module, name in names]
try:
    for module, name in names:
        setattr(module, name, object)
    assert eval(compile(tree, "<rebound>", "eval")) == 42
finally:
    for (module, name), original in zip(names, originals):
        setattr(module, name, original)
print("canonical AST identities and subclasses", True)


class ReportedExpression:
    body = ast.fix_missing_locations(ast.Constant(42))

    def __getattribute__(self, name):
        if name == "__class__":
            return ast.Expression
        return object.__getattribute__(self, name)


assert eval(compile(ReportedExpression(), "<reported-class>", "eval")) == 42
print("reported canonical AST class", True)


class PropertyConstant(ast.Constant):
    def __init__(self):
        self.lineno = 1
        self.col_offset = 0

    @property
    def value(self):
        return 42


assert eval(compile(ast.Expression(PropertyConstant()), "<property>", "eval")) == 42


class BrokenConstant(ast.Constant):
    def __getattribute__(self, name):
        if name == "value":
            raise LookupError("constant field lookup")
        return super().__getattribute__(name)


broken = BrokenConstant(42)
broken.lineno, broken.col_offset = 1, 0
try:
    compile(ast.Expression(broken), "<broken-field>", "eval")
except LookupError as error:
    assert str(error) == "constant field lookup"
else:
    raise AssertionError("AST field lookup exception was lost")
print("AST field hooks and descriptors", True)


class str:
    __xlang3_string_value__ = "6 * 7"


class bytes:
    __xlang3_bytes_value__ = b"6 * 7"


class Expression:
    body = ast.Constant(42)
    _fields = ("body",)


class Constant:
    value = 42
    _fields = ("value",)


class Add:
    _fields = ()


impostors = (str(), bytes(), Expression(),
             ast.fix_missing_locations(ast.Expression(Constant())),
             ast.fix_missing_locations(ast.Expression(
                 ast.BinOp(ast.Constant(20), Add(), ast.Constant(22)))))
for source in impostors:
    try:
        compile(source, "<impostor>", "eval")
    except TypeError:
        pass
    else:
        raise AssertionError("class name or fields substituted for native identity")
print("source and nested AST impostors rejected", True)
