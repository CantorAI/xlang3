class Target:
    def __init__(self, value):
        self.value = value

    def add(self, amount):
        return self.value + amount


class Dispatcher:
    def __init__(self, target):
        self.destination = target.add

    def double(self, amount):
        return amount * 2

    def call(self, amount):
        return self.destination(amount)


first = Target(10)
second = Target(20)
dispatcher = Dispatcher(first)
print(dispatcher.call(1))
print(dispatcher.call(2))
dispatcher.destination = dispatcher.double
print(dispatcher.call(3))
print(dispatcher.call(4))
dispatcher.destination = second.add
print(dispatcher.call(5))
second.value = 30
print(dispatcher.call(6))
