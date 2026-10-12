"""Exercise original unpickle inputs once; report eligibility inputs, no score."""
import importlib.util
from pathlib import Path
import sys
import types

ROOT = Path('D:/CantorAI/xlang3')
SITE = ROOT / 'venv/cpython3.14-a6792301b742-compat-31b33d68c68a/Lib/site-packages'
sys.path.insert(0, str(SITE))
# This is the unchanged official --pure-python backend selection.
sys.modules['_pickle'] = None
import pickle
import datetime
import _pydatetime
assert pickle.Pickler is pickle._Pickler and pickle.Unpickler is pickle._Unpickler
assert datetime.date is _pydatetime.date
source = Path('C:/Python/Python314/Lib/site-packages/pyperformance/data-files/benchmarks/bm_pickle/run_benchmark.py')
spec = importlib.util.spec_from_file_location('original_pickle_benchmark', str(source))
benchmark = importlib.util.module_from_spec(spec)
spec.loader.exec_module(benchmark)
date = benchmark.DICT['birthday']
selected_class, arguments = date.__reduce__()
assert selected_class is datetime.date and type(selected_class) is type
assert type(arguments) is tuple and len(arguments) == 1 and type(arguments[0]) is bytes and len(arguments[0]) == 4
raw_new = vars(selected_class)['__new__']
assert type(raw_new) is staticmethod and type(raw_new.__func__) is types.FunctionType
assert raw_new.__func__ is selected_class.__new__
assert selected_class.__new__.__code__.co_flags & (0x20 | 0x80 | 0x100 | 0x200) == 0
assert selected_class.__module__ == '_pydatetime'
assert sys.getprofile() is None and sys.gettrace() is None
assert all(sys.monitoring.get_events(tool) == 0 for tool in range(6))
assert benchmark.BENCHMARKS['unpickle'] == (benchmark.bench_unpickle, 20)
options = types.SimpleNamespace(protocol=pickle.HIGHEST_PROTOCOL)
assert options.protocol == 5
# Invoke the exact installed body once with all three original objects.
# Its elapsed return is deliberately discarded; this is an untimed eligibility
# check, not an official pyperf result or a constructor frequency estimate.
benchmark.bench_unpickle(1, pickle, options)
assert sys.getprofile() is None and sys.gettrace() is None
print('PASS original unpickle body and three inputs; pure backend protocol5')
print('PASS date REDUCE callable canonical Python class; exact tuple four-byte state')
print('PASS raw Python static new synchronous; no profile trace monitoring hooks')
