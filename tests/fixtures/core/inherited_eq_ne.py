class Comparable:
    def __init__(self, value):
        self.value = value

    def __eq__(self, other):
        if not isinstance(other, Comparable):
            return NotImplemented
        return self.value == other.value


class Child(Comparable):
    pass


left = Child(1)
same = Child(1)
different = Child(2)
print(left == same, left != same)
print(left == different, left != different)
