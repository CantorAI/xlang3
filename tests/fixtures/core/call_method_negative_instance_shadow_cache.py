class Worker:
    def compute(self, value):
        return "class", value + 1


def repeat(target, count):
    results = []
    for value in range(count):
        results.append(target.compute(value))
    return results


worker = Worker()
print(repeat(worker, 4))

# Adding a same-named instance field must invalidate a cached negative lookup.
worker.compute = lambda value: ("instance", value + 10)
print(repeat(worker, 3))

# Removing the shadow restores class-method lookup and invalidates again.
del worker.compute
print(repeat(worker, 3))

# A materialized __dict__ stays on generic lookup because its contents can
# change without passing through the instance attribute vector.
dictionary_worker = Worker()
dictionary_worker.__dict__["compute"] = lambda value: ("dict", value + 20)
print(repeat(dictionary_worker, 2))

# Repeated allocate/free at the same call site exercises allocator reuse while
# the caller's site cache is warm.
def churn(count):
    results = []
    for value in range(count):
        target = Worker()
        results.append(target.compute(value))
    return results


print(churn(5))

# Class mutation invalidates the method cache's class-version guard.
Worker.compute = lambda self, value: ("new class", value + 30)
print(repeat(Worker(), 2))
