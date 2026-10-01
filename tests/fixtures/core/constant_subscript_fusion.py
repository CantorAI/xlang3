events = []


class Indexed:
    def __getitem__(self, index):
        events.append(index)
        return "custom"


def make_indexed():
    events.append("make")
    return Indexed()


def tuple_from_call():
    return ("builtin",)


assert make_indexed()[0] == "custom"
assert events == ["make", 0]
assert tuple_from_call()[0] == "builtin"
