import copy
import time

mapping = {'value': 1}
sequence = [1, 2, 3, 4]
memo = {}

def measure(name, fn, count):
    for _ in range(100):
        fn()
    start = time.perf_counter()
    for _ in range(count):
        fn()
    print(f'{name} n={count}: {(time.perf_counter() - start):.6f}s')

measure('_keep_alive', lambda: copy._keep_alive(mapping, memo), 5000)
measure('_deepcopy_dict', lambda: copy._deepcopy_dict(mapping, memo, copy.deepcopy), 5000)
measure('_deepcopy_list', lambda: copy._deepcopy_list(sequence, memo, copy.deepcopy), 3000)
measure('deepcopy', lambda: copy.deepcopy(mapping), 3000)
