import gc
import weakref


class Parent:
    pass


def make_child_cycle():
    class Child(Parent):
        pass

    Child.self = Child
    return weakref.ref(Child)


def make_mutual_cycle():
    class A:
        pass

    class B:
        pass

    A.other = B
    B.other = A
    return weakref.ref(A), weakref.ref(B)


def make_local_parent_cycle():
    class LocalParent:
        pass

    class LocalChild:
        pass

    LocalParent.self = LocalParent
    LocalParent.child = LocalChild
    LocalChild.self = LocalChild
    return weakref.ref(LocalChild)


child = make_child_cycle()
mutual = make_mutual_cycle()
local_child = make_local_parent_cycle()
gc.collect()
print(child() is None)
print([ref() is None for ref in mutual])
print(local_child() is None)


# Canonical bindings are roots, but a rebound name must still find an alias.
class ModuleBound:
    pass


module_bound_ref = weakref.ref(ModuleBound)
gc.collect()
assert module_bound_ref() is ModuleBound
ModuleBound.__name__ = "RenamedModuleBound"
gc.collect()
assert module_bound_ref() is ModuleBound
ModuleBound.__name__ = "ModuleBound"
ModuleAlias = ModuleBound
ModuleBound = 17
gc.collect()
assert module_bound_ref() is ModuleAlias
del ModuleAlias
gc.collect()
assert module_bound_ref() is None


# A class retained only through the live module dictionary is also a root.
def make_dictionary_root():
    class DictionaryBound:
        pass

    globals()["dictionary_class_alias"] = DictionaryBound
    return weakref.ref(DictionaryBound)


dictionary_root_ref = make_dictionary_root()
gc.collect()
assert dictionary_root_ref() is globals()["dictionary_class_alias"]
del globals()["dictionary_class_alias"]
gc.collect()
assert dictionary_root_ref() is None
