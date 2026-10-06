import time

def identity(value):
    return value

def integer_loop(count):
    value = 0
    for _ in range(count):
        value += 1
    return value

def call_loop(count):
    value = 0
    for _ in range(count):
        value = identity(value)
    return value

def dict_loop(count):
    data = {"value": 0}
    for _ in range(count):
        data["value"] = data["value"] + 1
    return data["value"]

for name, body, count in [('integer loop', integer_loop, 50000), ('call loop', call_loop, 50000), ('dict loop', dict_loop, 10000)]:
    body(100)
    times = []
    for _ in range(3):
        start = time.perf_counter()
        body(count)
        times.append(time.perf_counter() - start)
    print(f'{name} n={count}: ' + ', '.join(f'{value:.6f}s' for value in times))
