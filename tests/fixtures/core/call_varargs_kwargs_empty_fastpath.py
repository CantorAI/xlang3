class C:
    def capture(self, value, *args, **kwargs):
        kwargs["own"] = value
        return args, kwargs


receiver = C()
first = receiver.capture(3)
second = receiver.capture(4)
print(first[0], first[1], second[0], second[1], first[1] is second[1])
