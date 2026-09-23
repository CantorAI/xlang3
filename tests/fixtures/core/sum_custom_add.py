class Value:
    def __init__(self, amount):
        self.amount = amount

    def __add__(self, other):
        return Value(self.amount + other.amount)


values = [Value(2), Value(3), Value(5)]
print(sum(iter(values), Value(10)).amount)
