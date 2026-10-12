import gc
import weakref


class Root:
    pass


class OtherRoot:
    pass


class Subject(Root):
    pass


class Child(Subject):
    pass


first = Subject.__mro__
assert Subject.__mro__ is first
Subject.marker = 1
Subject.__name__ = "RenamedSubject"
assert Subject.__mro__ is first
print("ordinary class writes preserve MRO tuple", True)


class Descriptor:
    def __get__(self, instance, owner):
        return None


class DescriptorOwner:
    member = Descriptor()


descriptor_mro = DescriptorOwner.__mro__
Descriptor.__get__ = lambda self, instance, owner: 1
assert DescriptorOwner.__mro__ is descriptor_mro
print("descriptor writes preserve MRO tuple", True)


Subject.__bases__ = (OtherRoot,)
second = Subject.__mro__
assert second is Subject.__mro__ and second is not first
assert first[1] is Root and second[1] is OtherRoot
child_mro = Child.__mro__
assert child_mro[1] is Subject and child_mro[2] is OtherRoot
Subject.__bases__ = (Root,)
third = Subject.__mro__
assert third is Subject.__mro__ and third is not second
assert Child.__mro__ is not child_mro
print("base changes replace descendant MRO tuples", True)


class X:
    pass


class Y:
    pass


class A(X, Y):
    pass


class B(Y, X):
    pass


class C(A):
    pass


before_failed_change = C.__mro__
failure_message = ""
try:
    C.__bases__ = (A, B)
except Exception as error:
    failure_message = str(error).lower()
else:
    raise AssertionError("inconsistent MRO assignment unexpectedly succeeded")
assert "consistent method resolution order" in failure_message
assert C.__mro__ is before_failed_change
print("failed base changes preserve prior MRO tuple", True)


def make_unretained_class():
    class Local:
        pass

    reference = weakref.ref(Local)
    Local.__mro__
    return reference


unretained = make_unretained_class()
gc.collect()
assert unretained() is None


def make_retained_class():
    class Local:
        pass

    return weakref.ref(Local), Local.__mro__


retained, old_tuple = make_retained_class()
gc.collect()
assert retained() is old_tuple[0]
del old_tuple
gc.collect()
assert retained() is None
print("cached MRO tuple participates in class collection", True)
