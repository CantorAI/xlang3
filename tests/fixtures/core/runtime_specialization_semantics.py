import sys


def numeric_augmented_assignment():
    value = 1
    value += 2
    value += 0.5
    return value


class CustomAdd:
    def __init__(self):
        self.calls = 0

    def __iadd__(self, value):
        self.calls += 1
        return ("custom", value, self.calls)


def custom_augmented_assignment():
    value = CustomAdd()
    value += 7
    return value


def conditional_augmented_assignment(use_number):
    value = CustomAdd()
    if use_number:
        value = 1
    value += 2
    return value


def mutable_augmented_assignment():
    value = [1]
    original = value
    value += [2]
    return value, value is original


class AttributeTarget:
    pass


attribute_target = AttributeTarget()
attribute_target.value = 1


def cached_attribute():
    return attribute_target.value


print("numeric-iadd", numeric_augmented_assignment())
print("custom-iadd", custom_augmented_assignment())
print("conditional-iadd", conditional_augmented_assignment(False), conditional_augmented_assignment(True))
print("mutable-iadd", mutable_augmented_assignment())
characters = ["a", "!", "[", " ", "~"]
print("ascii-cache", all(character[0] is character[0] for character in characters))
print("ascii-intern", all(sys.intern(character) is character[0] for character in characters))
print("cached-attribute", cached_attribute())
attribute_target.value = 2
print("mutated-attribute", cached_attribute())


class DataDescriptor:
    def __get__(self, instance, owner):
        return 9

    def __set__(self, instance, value):
        pass


AttributeTarget.value = DataDescriptor()
print("descriptor-invalidation", cached_attribute())
