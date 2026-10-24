"""Strict mutation/fallback proposal for an activation-local imported binding cache."""
import math as target
import math
from types import ModuleType


def make_module(name, answer):
    module = ModuleType(name)
    module.sqrt = lambda value: answer
    return module


first_module = make_module("binding_first", 1)
second_module = make_module("binding_second", 2)
third_module = make_module("binding_third", 3)


def stable_imported_loop():
    answers = []
    for _ in range(32):
        answers.append(target.sqrt(9))
    return answers


assert stable_imported_loop() == [3.0] * 32
print("PASS binding-warm")


def replace_global_loop():
    global target
    answers = []
    for index in range(9):
        if index == 0:
            target = first_module
        elif index == 3:
            target = second_module
        elif index == 6:
            del target
            target = third_module
        answers.append(target.sqrt(9))
    return answers


assert replace_global_loop() == [1] * 3 + [2] * 3 + [3] * 3
print("PASS binding-replace-delete-readd")


def namespace_mutation_loop():
    namespace = globals()
    answers = []
    for index in range(8):
        if index == 0:
            namespace["target"] = first_module
        elif index == 2:
            namespace["target"] = second_module
        elif index == 4:
            del namespace["target"]
            # Recreate the binding after unrelated slot growth; cache only its
            # live index, never the old vector element or compiled name index.
            for padding in range(40):
                namespace["binding_padding_" + str(padding)] = padding
            namespace["target"] = third_module
        answers.append(target.sqrt(9))
    return answers


assert namespace_mutation_loop() == [1, 1, 2, 2, 3, 3, 3, 3]
print("PASS binding-namespace-growth")


fallback_lookups = []


class Receiver:
    def __getattr__(self, name):
        assert name == "sqrt"
        fallback_lookups.append(name)
        # Replacing the last global receiver owner during attribute lookup
        # must preserve this call's owned fallback receiver/name arguments.
        globals()["target"] = second_module
        return lambda value: 100


def nonmodule_loop():
    global target
    answers = []
    for index in range(6):
        if index == 0:
            target = first_module
        elif index == 2:
            target = Receiver()
        answers.append(target.sqrt(9))
    return answers


assert nonmodule_loop() == [1, 1, 100, 2, 2, 2]
assert fallback_lookups == ["sqrt"]
print("PASS binding-nonmodule-reentry")


def local_module_loop():
    target = first_module
    answers = []
    for index in range(6):
        if index == 2:
            target = second_module
        elif index == 4:
            target = third_module
        answers.append(target.sqrt(9))
    return answers


assert local_module_loop() == [1, 1, 2, 2, 3, 3]
print("PASS binding-local-reassignment")


export_module = ModuleType("binding_native_export")
export_module.sqrt = math.sqrt
lazy_module = ModuleType("binding_lazy_export")
lazy_lookups = []
marker = LookupError("binding-original-error")


def lazy_export(name):
    assert name == "sqrt"
    lazy_lookups.append(name)
    return lambda value: 17


def fail_export(value):
    raise marker


lazy_module.__getattr__ = lazy_export


def export_loop():
    global target
    answers = []
    for index in range(8):
        if index == 0:
            target = export_module
        elif index == 2:
            export_module.sqrt = lambda value: 11
        elif index == 4:
            target = lazy_module
        elif index == 6:
            export_module.sqrt = fail_export
            target = export_module
        try:
            answers.append(target.sqrt(9))
        except LookupError as error:
            assert error is marker
            answers.append("original-error")
    return answers


assert export_loop() == [3.0, 3.0, 11, 11, 17, 17, "original-error", "original-error"]
assert lazy_lookups == ["sqrt", "sqrt"]
print("PASS binding-export-lazy-exception")
