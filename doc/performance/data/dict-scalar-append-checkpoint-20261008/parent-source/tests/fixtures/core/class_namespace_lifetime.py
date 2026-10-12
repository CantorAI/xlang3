"""Class-frame namespace ownership; no Ephemeral constructor is invoked."""
import gc
import sys
import weakref


def no_constructor():
    class Ephemeral:
        __slots__ = ('obj',)
        def __init__(self, obj):
            self.obj = obj
    class_ref = weakref.ref(Ephemeral)
    initializer_ref = weakref.ref(Ephemeral.__init__)
    del Ephemeral
    gc.collect()
    assert class_ref() is None and initializer_ref() is None


no_constructor()
print('completed class namespace releases initializer')


def capture_class_frame():
    class Kept:
        namespace = locals()
        body_frame = sys._getframe()
        def method(self):
            return 17
    return Kept


Kept = capture_class_frame()
assert Kept.namespace['method'] is Kept.method
assert Kept.body_frame.f_code.co_name == 'Kept'
assert Kept.body_frame.f_locals is Kept.namespace
assert Kept.body_frame.f_locals['method'] is Kept.method
saved_namespace, saved_frame = Kept.namespace, Kept.body_frame
method_ref = weakref.ref(Kept.method)
del Kept
gc.collect()
assert method_ref() is saved_namespace['method']
assert saved_frame.f_locals is saved_namespace
assert saved_namespace['method'](None) == 17
print('escaped class locals and frame retain namespace')


def annotation_holder():
    token = object()
    class Annotated:
        target = token
        value: target
        def method(self, arg: target) -> target:
            return arg
    return Annotated


Annotated = annotation_holder()
assert Annotated.__annotations__['value'] is Annotated.target
assert Annotated.method.__annotations__['arg'] is Annotated.target
assert Annotated.method.__annotations__['return'] is Annotated.target
print('lazy class and method annotations retain captures')


def drop_initializer(cls):
    initializer_ref = weakref.ref(cls.__init__)
    del cls.__init__
    gc.collect()
    assert initializer_ref() is None
    return cls


def decorated_holder():
    @drop_initializer
    class Decorated:
        def __init__(self, obj):
            self.obj = obj
    return Decorated


Decorated = decorated_holder()
assert '__init__' not in Decorated.__dict__
print('decorator observes completed class namespace retirement')
