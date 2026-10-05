"""Direct diagnostic matching pyperformance 1.14.0's unpickle loop shape.

This omits pyperf worker calibration and uses a compact equivalent data set;
its timings are useful for paired XLang3 path trials, not official scores.
"""
import datetime
import pickle
import random
import time

random_source = random.Random(5)
dictionary = {
    "birthday": datetime.date(1980, 5, 7),
    "tags": ["a", "b", "c", "d", "e", "f", "g"],
    "id": 302935349,
    "time_created": 1225237014,
    "time_updated": 1233134493,
}
tuple_value = ([265867233, 265868503, 265252341, 265243910, 265879514], 60)
dictionary_group = []
for _ in range(3):
    item = dict(dictionary)
    item["random"] = random_source.random()
    dictionary_group.append(item)
loads = pickle.loads
payloads = tuple(pickle.dumps(obj, protocol=5) for obj in (dictionary, tuple_value, dictionary_group))
loops = 300
range_it = range(loops)
start = time.perf_counter()
for _ in range_it:
    for payload in payloads:
        # Match bm_pickle/bench_unpickle's twenty unrolled loads per object.
        loads(payload)
        loads(payload)
        loads(payload)
        loads(payload)
        loads(payload)
        loads(payload)
        loads(payload)
        loads(payload)
        loads(payload)
        loads(payload)
        loads(payload)
        loads(payload)
        loads(payload)
        loads(payload)
        loads(payload)
        loads(payload)
        loads(payload)
        loads(payload)
        loads(payload)
        loads(payload)
elapsed = time.perf_counter() - start
print(f"pyperformance unpickle shape: {elapsed / loops * 1e6:.1f} us/loop; payloads={[len(p) for p in payloads]}")
