import operator


class Descriptor:
    def __get__(self, instance, owner):
        return "descriptor"


class Target:
    value = Descriptor()


with_value = Target()
with_value.value = "instance"
without_value = Target()

print(getattr(with_value, "value"))
print(operator.attrgetter("value")(with_value))
print(getattr(without_value, "value"))
