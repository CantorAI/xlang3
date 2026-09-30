from collections import deque


class Child(deque):
    pass


print(deque[int].__origin__ is deque)
print(Child[str].__origin__ is Child)
