class EqualByNumber:
    def __init__(self, number):
        self.number = number

    def __eq__(self, other):
        return isinstance(other, EqualByNumber) and self.number == other.number


values = {"one": {"x": 1}, "two": EqualByNumber(2)}.values()
print({"x": 1} in values, {"x": 2} in values)
print(EqualByNumber(2) in values, EqualByNumber(3) in values)
