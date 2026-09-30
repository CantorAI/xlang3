import sys


class Item:
    def __init__(self, value):
        self.value = value

    @classmethod
    def less(cls, left, right):
        return left.value < right.value


def invoke(owner, left, right):
    return owner.less(left, right)


left = Item(2)
right = Item(5)
print("initial", invoke(Item, left, right))
for _ in range(40):
    invoke(Item, left, right)
left.value = 8
print("updated-values", invoke(Item, left, right))


def reversed_less(cls, left, right):
    return left.value > right.value


Item.less = classmethod(reversed_less)
print("replaced-method", invoke(Item, left, right))


class Child(Item):
    pass


child_left = Child(1)
child_right = Child(3)
print("inherited-method", invoke(Child, child_left, child_right))


def always_false(cls, left, right):
    return False


Item.less = classmethod(always_false)
print("mutated-base-method", invoke(Child, child_left, child_right))


def child_less(cls, left, right):
    return "child override"


Child.less = classmethod(child_less)
print("subclass-override", invoke(Child, child_left, child_right))


marker = object()


class UserComparison:
    def __lt__(self, other):
        return marker


def original_less(cls, left, right):
    return left.value < right.value


Item.less = classmethod(original_less)
custom_result = invoke(Item, Item(UserComparison()), Item(UserComparison()))
print("custom-comparison-result", custom_result is marker)


events = []


def profile(frame, event, arg):
    if frame.f_code.co_name == "original_less":
        events.append(event)


sys.setprofile(profile)
profiled_result = invoke(Item, Item(1), Item(2))
sys.setprofile(None)
print("profile-preserved", "call" in events, "return" in events, profiled_result)
