class Items:
    def __iter__(self):
        yield 2
        yield 3


class Indexed:
    def __getitem__(self, index):
        if index >= 2:
            raise IndexError
        return index + 4


print((1, *Items()))
print([1, *Items()])
print(sorted({1, *Items()}))
print((1, *Indexed()))
