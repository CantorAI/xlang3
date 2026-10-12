# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Generic property getter callability, mutation, ownership and errors."""
import gc
import operator
import sys
import weakref


def read_value(receiver):
    return receiver.value


class Getter:
    def __call__(self, receiver):
        return receiver.payload


class Box:
    def __init__(self, payload):
        self.payload = payload

    value = property(Getter())


box = Box(("id", "name"))
assert callable(Box.value.fget)
for unused in range(4):
    assert read_value(box) == ("id", "name")
assert getattr(box, "value") == Box.value.__get__(box, Box) == box.payload
Box.value = property(operator.attrgetter("payload"))
for unused in range(4):
    assert read_value(box) == box.payload
print("PASS property_callable_instance_and_attrgetter")


class BoundGetter:
    def get(self, receiver):
        return receiver.payload + 1


class Result:
    def __init__(self, receiver):
        self.payload = receiver.payload


box.payload = 7
Box.value = property(BoundGetter().get)
assert read_value(box) == 8
Box.value = property(staticmethod(lambda receiver: receiver.payload + 2))
assert read_value(box) == 9
Box.value = property(Result)
assert read_value(box).payload == 7
# Keep the existing function and native-function getter paths covered.
Box.value = property(lambda receiver: receiver.payload + 3)
assert read_value(box) == 10
Box.value = property(id)
assert read_value(box) == id(box)
Box.value = property(type)
assert read_value(box) is Box
print("PASS property_callable_kinds_and_existing_paths")


class Replacement:
    def __call__(self, receiver):
        return "replacement"


class MutatingGetter:
    def __call__(self, receiver):
        type(receiver).value = property(Replacement())
        return "original"


Box.value = property(MutatingGetter())
assert read_value(box) == "original"
assert read_value(box) == "replacement"


def replacement_call(self, receiver):
    return "changed call"


Replacement.__call__ = replacement_call
assert read_value(box) == "changed call"
del Box.value
try:
    read_value(box)
except AttributeError:
    pass
else:
    raise AssertionError("deleted property remained visible")
Box.value = property(Getter())
assert read_value(box) == 7
print("PASS property_callable_lookup_mutation")


marker = LookupError("callable getter marker")


class FailingGetter:
    def __call__(self, receiver):
        raise marker


Box.value = property(FailingGetter())
try:
    read_value(box)
except LookupError as caught:
    assert caught is marker
    names = []
    traceback = caught.__traceback__
    while traceback is not None:
        names.append(traceback.tb_frame.f_code.co_name)
        traceback = traceback.tb_next
    assert "__call__" in names and "read_value" in names
else:
    raise AssertionError("getter exception was lost")
marker.__traceback__ = None
Box.value = property(41)
try:
    read_value(box)
except TypeError:
    pass
else:
    raise AssertionError("noncallable getter did not raise TypeError")
Box.value = property()
try:
    read_value(box)
except AttributeError:
    pass
else:
    raise AssertionError("missing getter did not raise AttributeError")
print("PASS property_callable_exception_identity_and_traceback")


released = []


def lifetime_case():
    class OwnerGetter:
        def __call__(self, receiver):
            assert reference() is self and not released
            type(receiver).value = property(Getter())
            gc.collect()
            assert reference() is self and not released
            return receiver.payload

        def __del__(self):
            released.append("getter")

    Box.value = property(OwnerGetter())
    reference = weakref.ref(Box.value.fget)
    local = Box("alive")
    assert read_value(local) == "alive"
    assert read_value(local) == "alive"
    return reference


getter_reference = lifetime_case()
gc.collect()
assert getter_reference() is None and released == ["getter"]
print("PASS property_callable_selected_owner_lifetime")


observer_events = []
getter_code = Getter.__call__.__code__


def profile(frame, event, arg):
    if frame.f_code is getter_code and event in ("call", "return"):
        observer_events.append(event)


Box.value = property(Getter())
sys.setprofile(profile)
try:
    assert read_value(box) == 7
finally:
    sys.setprofile(None)
assert observer_events == ["call", "return"]
observer_events.clear()


def trace(frame, event, arg):
    if frame.f_code is getter_code and event in ("call", "return"):
        observer_events.append(event)
    return trace


sys.settrace(trace)
try:
    assert read_value(box) == 7
finally:
    sys.settrace(None)
assert observer_events == ["call", "return"]
print("PASS property_callable_observer_frames")
