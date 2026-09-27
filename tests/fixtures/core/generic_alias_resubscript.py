type Inferred[T] = list[T]
print(repr(Inferred.__type_params__[0]), Inferred.__type_params__[0].__infer_variance__, repr(Inferred.__value__))

try:
    list[int][str]
except TypeError as error:
    print(type(error).__name__, str(error))
