class A[T]:
    print(T.__name__, "T" in locals())

    def f(self, value: T) -> T:
        return value


print(A.__type_params__[0].__name__)
print(A.f.__annotate__(1)["value"] is A.__type_params__[0])
print(A.f.__annotations__["return"] is A.__type_params__[0])
