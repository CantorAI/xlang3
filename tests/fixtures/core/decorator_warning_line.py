import ast
import linecache
import warnings


def deprecate(label):
    warnings.warn(label, DeprecationWarning, stacklevel=2)
    return lambda function: function


def define():
    with warnings.catch_warnings(record=True) as captured:
        warnings.simplefilter("always")

        class Example:
            @deprecate("old")
            def method(self):
                return 1

    warning = captured[0]
    print(warning.category.__name__, str(warning.message))
    print(linecache.getline(warning.filename, warning.lineno).strip())
    return Example


print(define()().method())

source = (
    "def define_ast():\n"
    "    with warnings.catch_warnings(record=True) as captured:\n"
    "        warnings.simplefilter('always')\n"
    "        class Example:\n"
    "            @deprecate('ast')\n"
    "            @classmethod\n"
    "            def method(self):\n"
    "                return 2\n"
    "    return captured[0]\n"
)
filename = "<decorator-ast>"
linecache.cache[filename] = (len(source), None, source.splitlines(True), filename)
namespace = {"warnings": warnings, "deprecate": deprecate}
exec(compile(ast.parse(source), filename, "exec"), namespace)
warning = namespace["define_ast"]()
print(linecache.getline(warning.filename, warning.lineno).strip())
