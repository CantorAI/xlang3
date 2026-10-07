from functools import partial


class Items(list):
    def append(self, item):
        # html5lib uses this Python override and the native base descriptor.
        list.append(self, item)

    def through_super(self, item):
        super().append(item)


values = Items()
for index in range(5):
    values.append(index)
values.through_super(5)
unbound = list.append
unbound(values, 6)
getattr(list, "append")(values, 7)
unbound(*(values, 8))
partial(list.append, values)(9)
bound = values.append
bound(10)
assert values == list(range(11))

plain = []
for index in range(5):
    plain.append(index)
    list.append(plain, index)
assert plain == [0, 0, 1, 1, 2, 2, 3, 3, 4, 4]

for call in (
    lambda: list.append(plain),
    lambda: list.append(plain, 1, 2),
    lambda: list.append(1, 2),
    lambda: plain.append(),
    lambda: plain.append(1, 2),
):
    before = list(plain)
    try:
        call()
    except TypeError:
        pass
    else:
        assert False, "invalid append call did not raise TypeError"
    assert plain == before

print("native unbound list append and subclass override ok")
