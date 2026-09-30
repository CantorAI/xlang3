class Counter:
    def __init__(self):
        self._value = 1

    @property
    def value(self):
        return self._value + 1

    @value.setter
    def value(self, item):
        self._value = item - 1

    @value.deleter
    def value(self):
        self._value = 1


def main():
    counter = Counter()
    total = 0
    i = 0
    while i < 101:
        counter.value = i
        total = total + counter.value
        if i % 10 == 0:
            del counter.value
            total = total + counter.value
        i = i + 1
    print(total)


main()
