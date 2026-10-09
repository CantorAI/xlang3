"""Profile call counts for one original body; elapsed values are discarded."""
import collections
import json
from pathlib import Path
import runpy
import sys

sys.path.insert(0, 'D:/CantorAI/xlang3/venv/cpython3.14-a6792301b742-compat-31b33d68c68a/Lib/site-packages')
assert sys.version_info[:3] == (3, 14, 7)
assert sys.gettrace() is None and sys.getprofile() is None
original = Path('C:/Python/Python314/Lib/site-packages/pyperformance/data-files/benchmarks/bm_pickle/run_benchmark.py')
namespace = runpy.run_path(str(original))
sys.modules['_pickle'] = None
import pickle
assert pickle.Pickler.__module__ == 'pickle' and pickle.loads.__module__ == 'pickle'

class Options:
    protocol = 5

python_calls = collections.Counter()
native_calls = collections.Counter()

def count(frame, event, arg):
    if event == 'call':
        code = frame.f_code
        python_calls[(code.co_filename, code.co_name)] += 1
    elif event == 'c_call':
        try:
            name = arg.__qualname__
        except AttributeError:
            try:
                name = arg.__name__
            except AttributeError:
                name = '<unsupported native name>'
        native_calls[name] += 1

# Warm outside profiling, then count the unchanged original 60-load body.
# Its returned elapsed time is ignored; profiling changes optimization policy.
namespace['bench_unpickle'](1, pickle, Options())
sys.setprofile(count)
try:
    namespace['bench_unpickle'](1, pickle, Options())
finally:
    sys.setprofile(None)
print(json.dumps({'implementation': sys.implementation.name, 'protocol': 5, 'original_loops': 1,
    'loads_per_original_body': 60, 'official_inner_loops_normalization': 20,
    'scored': False, 'timings_recorded': False,
    'python_calls': [{'file': file, 'function': function, 'count': value}
        for (file, function), value in sorted(python_calls.items(), key=lambda item: (-item[1], item[0]))],
    'native_calls': [{'name': name, 'count': value} for name, value in native_calls.most_common()],
    'scope': 'Profiled call counts only. The original function also creates serialized payloads before its internal timer; those setup calls are included in this profile. Profiling disables optimizations, so counts do not establish unprofiled fast-path hits or CPU shares.'}, sort_keys=True))
