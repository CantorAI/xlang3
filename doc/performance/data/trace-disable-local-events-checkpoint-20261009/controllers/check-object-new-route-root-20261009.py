"""Untimed CPython-first semantics and emitted-IR check for lookup routes."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path('D:/CantorAI/xlang3')
CP = Path('C:/Python/Python314/python.exe')
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
DATA = ROOT / 'doc/performance/data'
PREFIX = 'object-new-route-untimed-20261009'
FOLDER = DATA / PREFIX
RECEIPT = DATA / (PREFIX + '.json')
DRAFT = ROOT / 'scratch/performance/object-new-static-lookup-diagnostic-child-proposed-20261009.py'
CHILD = ROOT / 'scratch/performance/object-new-static-lookup-diagnostic-child-20261009.py'
CORRECTNESS = DATA / 'nested-trace-setting-correctness-r2-20261009.json'

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def tree(folder):
    return {p.relative_to(folder).as_posix(): sha(p) for p in folder.rglob('*') if p.is_file()}

assert Path(sys.executable).resolve() == CP.resolve()
assert sys.version_info[:3] == (3, 14, 7) and sys.flags.isolated
assert sha(DRAFT) == 'b0682e6c7c457fd8c23e924d89d5f4e11e3926babf1299ae9caad2a814a6b6c2'
assert sha(CORRECTNESS) == 'de6a31342817c406d103cc9811fc5d01f82e036bf4cb6c0495ac1e3e1743619b'
correct = json.loads(CORRECTNESS.read_bytes())
assert correct['status'] == 'correctness_passed_performance_pending'
assert tree(RELEASE) == correct['release_sha256']
assert not RECEIPT.exists() and not FOLDER.exists() and not CHILD.exists()

# Keep the reviewed two-body diagnostic; add an explicit path that executes
# one semantic check per route and cannot enter either timed loop.
source = DRAFT.read_text(encoding='utf-8')
old = '    args = parser.parse_args()'
assert source.count(old) == 1
source = source.replace(old, '    parser.add_argument("--verify-only", action="store_true")\n' + old)
old = '''        start = time.perf_counter()
        for _ in iterations:
            value = function(EntryOnly, STATE)
        elapsed = time.perf_counter() - start

        check(value)
        assert elapsed > 0
        seconds[name] = elapsed'''
assert source.count(old) == 1
source = source.replace(old, '''        if not args.verify_only:
            start = time.perf_counter()
            for _ in iterations:
                value = function(EntryOnly, STATE)
            elapsed = time.perf_counter() - start
            check(value)
            assert elapsed > 0
            seconds[name] = elapsed''')
source = source.replace('"terminal": True,', '"terminal": True,\n        "verify_only": args.verify_only,')
source = source.replace('"timed_operation_count": 2 * OPERATIONS,', '"timed_operation_count": 0 if args.verify_only else 2 * OPERATIONS,')
CHILD.write_text(source, encoding='utf-8', newline='\n')
compile(source, str(CHILD), 'exec')
FOLDER.mkdir()
pins = {str(ROOT / p): h for p, h in correct['source_sha256'].items()}
pins.update({str(RELEASE / p): h for p, h in correct['release_sha256'].items()})
for p in (CP, CP.with_name('python314.dll'), DRAFT, CHILD, CORRECTNESS, Path(__file__),
          ROOT / 'src/runtime/attribute.cpp', ROOT / 'src/builtins/object_type_builtins.cpp'):
    pins[str(p)] = sha(p)
assert all(sha(p) == h for p, h in pins.items())
record = {'terminal': False, 'status': 'checking', 'timed': False,
          'candidate': 'current trace-repair build, not the earlier measured R4',
          'pins_before': pins, 'phases': [], 'checks': {}}

def save():
    RECEIPT.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8', newline='\n')

env = os.environ.copy()
for name in ('PYTHONPATH', 'PYTHONPYCACHEPREFIX', 'PYTHONIOENCODING', 'PYTHONOPTIMIZE', 'PYTHONHOME'):
    env.pop(name, None)
env['XLANG3_PYTHON_LIB'] = str(CP.parent / 'Lib')

def run(label, exe, extra, selection):
    dll = exe.with_name('python314.dll' if selection == 'cpython-python' else 'xlang3_runtime.dll')
    command = [str(exe), *extra, str(CHILD), '--selection', selection,
               '--case-order', 'original_lookup,saved_native_lookup', '--runtime-executable', str(exe),
               '--exe-sha256', sha(exe), '--dll-sha256', sha(dll), '--source-sha256', sha(CHILD), '--verify-only']
    out, err = FOLDER / (label + '.stdout.log'), FOLDER / (label + '.stderr.log')
    local = env.copy()
    local['PATH'] = str(exe.parent) + os.pathsep + local.get('PATH', '')
    with out.open('xb') as stdout, err.open('xb') as stderr:
        child = subprocess.Popen(command, cwd=ROOT, env=local, stdin=subprocess.DEVNULL,
                                 stdout=stdout, stderr=stderr, creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            result = child.wait(timeout=120)
        finally:
            if child.poll() is None:
                subprocess.run(['taskkill', '/F', '/T', '/PID', str(child.pid)], capture_output=True, timeout=10)
                child.wait(timeout=10)
    row = {'label': label, 'command': command, 'exit_code': result,
           'stdout': out.name, 'stdout_sha256': sha(out), 'stderr': err.name, 'stderr_sha256': sha(err)}
    record['phases'].append(row)
    save()
    assert result == 0, err.read_text(errors='replace')[-2000:]
    parsed = json.loads(out.read_text(encoding='utf-8'))
    assert parsed['verify_only'] and parsed['timed_operation_count'] == 0 and parsed['seconds'] == {}
    assert parsed['hashes_unchanged'] and parsed['same_allocated_class']
    row['semantic_result'] = parsed
    save()

try:
    save()
    run('cpython-first', CP, ['-I'], 'cpython-python')
    ir_dir = FOLDER / 'ir'
    ir_dir.mkdir()
    run('xlang3-ir', RELEASE / 'xlang3.exe', ['--dump-ir', '--debug-dir', str(ir_dir)], 'xlang3-current')
    ir = ir_dir / (CHILD.stem + '.ir.txt')
    text = ir.read_text(encoding='utf-8')
    blocks = {m.group(1): m.group(0) for m in re.finditer(r'^function #\d+ ([^\n]+)\n.*?(?=^function #|\Z)', text, re.M | re.S)}
    def block(suffix):
        choices = [b for name, b in blocks.items() if name == suffix or name.endswith('.' + suffix)]
        assert len(choices) == 1, (suffix, list(blocks))
        return choices[0]
    original = block('EntryOnly.__new__')
    saved = block('saved_native_wrapper')
    main = block('main')
    record['ir_sha256'] = sha(ir)
    record['checks'] = {'original_callmethod': 'CallMethod' in original and '__new__' in original,
                        'saved_direct_call': bool(re.search(r'\bCall\b', saved)) and 'CallMethod' not in saved,
                        'shared_wrapper_callsite': bool(re.search(r'\bCall\b', main)),
                        'both_python_bodies_present': bool(original) and bool(saved)}
    assert all(record['checks'].values()), record['checks']
    record['status'] = 'untimed_semantics_and_ir_passed'
except BaseException as error:
    record.update(status='untimed_check_failed', error=repr(error))
finally:
    record['terminal'] = True
    record['pins_after'] = {p: sha(p) for p in pins}
    record['hashes_unchanged'] = record['pins_after'] == pins
    record['artifacts_sha256'] = tree(FOLDER)
    if not record['hashes_unchanged']:
        record['status'] = 'invalid_hash_drift'
    save()
print(json.dumps({'status': record['status'], 'checks': record['checks'], 'receipt_sha256': sha(RECEIPT),
                  'error': record.get('error')}, indent=2), flush=True)
raise SystemExit(0 if record['status'] == 'untimed_semantics_and_ir_passed' else 1)
