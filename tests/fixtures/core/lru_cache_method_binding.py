from functools import lru_cache


class Cached:
    def __init__(self):
        self.calls = 0

    @lru_cache(maxsize=None)
    def add(self, value, *, offset=0):
        self.calls += 1
        return value + offset


cached = Cached()
print(cached.add(4, offset=2), cached.add(4, offset=2), cached.calls)
print(cached.add(5, offset=1), cached.add(4, offset=2), cached.calls)
