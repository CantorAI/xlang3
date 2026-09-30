class Compared:
    def __init__(self, value):
        self.value = value

    def __eq__(self, other):
        return self.value == other.value

    def __lt__(self, other):
        return self.value < other.value


# Exact scalar tuples use the VM's direct lexicographic path, including mixed
# bool/int/float values and the ordinary string ordering.
assert (False, 1, "a") < (False, 2, "a")
assert (True, 0) == (1, 0)
assert (1, 2.5) < (1, 3)
assert ("a", 9) < ("b", 0)
assert (1, 2) < (1, 2, 0)

# User-defined rich comparisons must continue through runtime dispatch.
assert (Compared(1),) < (Compared(2),)
assert (Compared(1),) == (Compared(1),)
