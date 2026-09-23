from array import ArrayType, array


class Index:
    def __index__(self):
        return 3


source = array("H", [1, 2])
print("ArrayType", ArrayType is array, isinstance(source, ArrayType))
for result in (source * Index(), Index() * source, source * 0, source * -2):
    print(result.typecode, list(result), result.tobytes().hex())
print("source", list(source))

alias = source
source *= 2
print("imul", alias is source, list(source), source.typecode)

try:
    source * 1.5
except Exception as exc:
    print(type(exc).__name__)
