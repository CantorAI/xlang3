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
