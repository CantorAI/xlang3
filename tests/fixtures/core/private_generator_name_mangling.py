class Snapshot:
    def __init__(self):
        self.__preserve = lambda value: value % 2 == 0

    def restore(self):
        return list(value for value in range(5) if self.__preserve(value))

    def nested(self):
        return list((value, self.__preserve(value)) for value in range(3))


snapshot = Snapshot()
print(snapshot.restore())
print(snapshot.nested())
