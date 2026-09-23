import types


class Descriptor:
    def __get__(self, instance, owner):
        return instance.__name__ + ":resolved"


class LazyModule(types.ModuleType):
    answer = Descriptor()


module = LazyModule("demo")
print(module.answer)
print(getattr(module, "answer"))
print(type(module) is LazyModule)
