import weakref


class Target:
    def __init__(self):
        self.hash_calls = 0

    def __hash__(self):
        self.hash_calls += 1
        return 31


target = Target()
ref = weakref.ref(target)
print(hash(ref), hash(ref), target.hash_calls)
