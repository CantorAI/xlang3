"""Count Python frame entries for pyperformance 1.14's json.dumps workload.

The four inputs and iteration counts match bm_json_dumps. sys.setprofile adds
call-event overhead, so this identifies call counts only; it does not produce
timings or a benchmark score.
"""
import json
import sys
from collections import Counter


empty = {}
simple = {"key1": 0, "key2": True, "key3": "value", "key4": "foo", "key5": "string"}
nested = {
    "key1": 0,
    "key2": simple,
    "key3": "value",
    "key4": simple,
    "key5": simple,
    "key": "\u0105\u0107\u017c",
}
data = [(empty, 2000), (simple, 1000), (nested, 1000), ([nested] * 1000, 1)]
counts = Counter()


def count_python_frames(frame, event, arg):
    if event == "call":
        code = frame.f_code
        counts[(code.co_filename, code.co_name)] += 1


sys.setprofile(count_python_frames)
for obj, count in data:
    for _ in range(count):
        json.dumps(obj)
sys.setprofile(None)

for (filename, name), count in sorted(
    counts.items(), key=lambda item: (-item[1], item[0][1])
):
    if name in ("dumps", "encode", "iterencode"):
        print(count, name, filename)
print("profiled Python frame entries", sum(counts.values()))
