"""Inspect untouched official input objects and operation counts; no timers."""
import ast
import hashlib
import importlib.util
import json
from pathlib import Path
import sys

ROOT = Path('D:/CantorAI/xlang3')
CP = Path('C:/Python/Python314/python.exe')
SITE = ROOT / 'venv/cpython3.14-a6792301b742-compat-31b33d68c68a/Lib/site-packages'
SOURCE = SITE / 'pyperformance/data-files/benchmarks/bm_pickle/run_benchmark.py'
OUT = ROOT / 'doc/performance/data/date-native-boundary-original-pickle-inputs-20261009.json'
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
assert Path(sys.executable).resolve() == CP.resolve()
assert sys.version_info[:3] == (3, 14, 7) and sys.flags.isolated
assert not OUT.exists()
before = sha(SOURCE)
parsed = ast.parse(SOURCE.read_bytes())
function = next(n for n in parsed.body if isinstance(n, ast.FunctionDef) and n.name == 'bench_pickle')
dump_calls = sum(isinstance(n, ast.Call) and isinstance(n.func, ast.Name) and n.func.id == 'dumps' for n in ast.walk(function))
sys.path.insert(0, str(SITE))
spec = importlib.util.spec_from_file_location('date_workload_inspection', SOURCE)
benchmark = importlib.util.module_from_spec(spec)
spec.loader.exec_module(benchmark)
import datetime

def date_count(value):
    if type(value) is datetime.date:
        return 1
    if isinstance(value, dict):
        return sum(date_count(k) + date_count(v) for k, v in value.items())
    if isinstance(value, (list, tuple)):
        return sum(date_count(v) for v in value)
    return 0

counts = {name: date_count(getattr(benchmark, name)) for name in ('DICT', 'TUPLE', 'DICT_GROUP')}
assert counts == {'DICT': 1, 'TUPLE': 0, 'DICT_GROUP': 0}
assert dump_calls == benchmark.BENCHMARKS['pickle'][1] == 20
assert sha(SOURCE) == before
record = dict(status='untimed_original_pickle_input_inspection_passed', terminal=True,
              executable=str(CP), version_info=list(sys.version_info[:3]),
              benchmark_source=str(SOURCE), benchmark_sha256=before,
              controller_sha256=sha(Path(__file__)), date_occurrences_per_object=counts,
              static_dump_call_sites_per_object=dump_calls, objects_per_outer_iteration=3,
              dumps_per_outer_iteration=60, pyperf_inner_loops=20,
              source_unchanged=True, timed=False, workload_cost_fraction_estimated=False,
              limitation='Input occurrence and AST call counts establish workload structure; they do not measure executed reducer counts or time shares.')
OUT.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
print(json.dumps(record))
