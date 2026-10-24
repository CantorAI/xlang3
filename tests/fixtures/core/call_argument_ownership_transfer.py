released = []


class Marker:
    def __del__(self):
        released.append("released")


def consume_and_delete(value):
    del value
    print("inside", len(released))


def make_marker():
    return Marker()


consume_and_delete(make_marker())
print("after", len(released))

kept = Marker()


def consume_pair(first, second):
    del first, second
    print("pair-inside", len(released))


consume_pair(kept, kept)
print("pair-after", len(released))


class MethodConsumer:
    def consume(self, value):
        del value
        print("method-inside", len(released))


MethodConsumer().consume(make_marker())
print("method-after", len(released))

method_kept = Marker()


class MethodPair:
    def consume(self, first, second):
        del first, second
        print("method-pair-inside", len(released))


MethodPair().consume(method_kept, method_kept)
print("method-pair-after", len(released))
