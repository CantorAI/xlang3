"""Unscored strict dictionary/Python checks; retain the actual preceding CPP pass."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
from datetime import datetime, timezone

ROOT = Path('D:/CantorAI/xlang3')
DATA = ROOT / 'doc/performance/data'
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
CP = Path('C:/Python/Python314/python.exe')
APP = DATA / 'dict-scalar-append-runtime-index-r3-applied-source-20261008.json'
APP_SHA = 'dc1b869fe0475e3bdd017aa3b667f755cb7a4dd7ad881494f8afb34af387f45e'
PROOF = ROOT / 'scratch/performance/dict-scalar-append-runtime-index-r3-proposed-20261008-provenance.json'
PROOF_SHA = '2d8b38ea6a87df13511201586e9e814b89b180444c34911d0ee26db9782e7614'
PARENT = ROOT / 'build-repro/controls/dict-scalar-append-runtime-index-parent-20261008'
PREFIX = 'dict-scalar-append-runtime-index-r3-focused-20261008'
NAMES = ('dict_hash_index', 'dict_integer_index_getitem', 'dict_intrinsic_write_index',
         'dict_custom_hash_equality', 'dict_get_missing_semantics',
         'dict_get_exception_preservation', 'hash_exception_preservation',
         'call_method_dict_cache_touch', 'pickle_module')
sha = lambda path: hashlib.sha256(Path(path).read_bytes()).hexdigest()
read = lambda path: json.loads(Path(path).read_bytes())
tree = lambda root: {p.relative_to(root).as_posix(): sha(p) for p in sorted(root.rglob('*')) if p.is_file()}

def main():
    assert sys.version_info[:3] == (3, 14, 7) and Path(sys.executable).resolve() == CP.resolve()
    assert not sys.flags.optimize and sha(APP) == APP_SHA and sha(PROOF) == PROOF_SHA
    output = DATA / (PREFIX + '.json')
    assert not output.exists()
    app, proof = read(APP), read(PROOF)
    parent = read(PARENT / 'preserved-release-provenance.json')
    sources = dict(parent['source_snapshot_sha256'], **proof['candidate_source_sha256'])
    assert sources == app['source_sha256'] and len(sources) == 115
    release = tree(RELEASE)
    assert len(release) == 178
    assert release['xlang3_runtime.dll'] == '19218af32ff4fdfb802144a713591c2126fcc0f1233ae04a08cfecc975922523'
    assert release['xlang3_interpreter_tests.exe'] == 'a269c3fab01c7fb6118484e7b9941438706223986d12a4468aa643b4cba32cf4'
    pins = {str(ROOT / path): value for path, value in sources.items()}
    pins.update({str(APP): APP_SHA, str(PROOF): PROOF_SHA, str(Path(__file__).resolve()): sha(__file__)})
    protected = {str(RELEASE): release, str(PARENT / 'Release'): parent['files_sha256'],
                 str(PARENT / 'source-snapshot'): parent['source_snapshot_sha256'],
                 str(ROOT / 'build-repro/Release'): parent['fixed_baseline_sha256']}
    def stable():
        return all(Path(p).is_file() and sha(p) == value for p, value in pins.items()) and all(tree(Path(p)) == values for p, values in protected.items())
    assert stable()
    record = dict(status='running_targeted_correctness', terminal=False, scored=False,
                  source_inventory_sha256=APP_SHA, source_sha256=sources, source_count=115,
                  binaries_sha256={RELEASE.relative_to(ROOT).as_posix()+'/'+p: value for p,value in release.items()},
                  candidate_binary_sha256=dict(exe=release['xlang3.exe'], dll=release['xlang3_runtime.dll']),
                  phases=[], started_utc=datetime.now(timezone.utc).isoformat(),
                  scope='Fresh original Python outputs; actual root CPP exit0 retained, not rerun. No timing claims.')
    def save(): output.write_text(json.dumps(record, indent=2)+'\n', encoding='utf-8')
    cpp_prefix = 'dict-scalar-append-runtime-index-r3-cpp-preliminary-20261008'
    cpp = dict(name='cpp', command=[str(RELEASE/'xlang3_interpreter_tests.exe')], passed=True, exit_code=0,
               timeout=False, actual_root_tool_exit_code=0)
    for stream in ('stdout','stderr'):
        path = DATA / (cpp_prefix+'.'+stream+'.log')
        assert path.read_bytes() == b''
        cpp[stream+'_log'], cpp[stream+'_sha256'] = path.name, sha(path)
        pins[str(path)] = sha(path)
    record['phases'].append(cpp)
    env = dict(os.environ, XLANG3_PYTHON_LIB=str(CP.parent/'Lib'),
               PYTHONPATH=str(ROOT/'benchmarks/diagnostics/pyperf_compat'), PYTHONIOENCODING='utf-8', PYTHONUNBUFFERED='1')
    env.pop('PYTHONOPTIMIZE', None)
    try:
        for name in NAMES:
            source, expected = f'tests/fixtures/core/{name}.py', f'tests/fixtures/expected/{name}.out'
            for path in (source, expected): pins[str(ROOT/path)] = sha(ROOT/path)
            assert stable()
            row = dict(name=name, source=source, expected=expected, source_sha256=sha(ROOT/source),
                       expected_sha256=sha(ROOT/expected), command=[str(RELEASE/'xlang3.exe'),str(ROOT/source)],
                       passed=False, timeout=False)
            record['phases'].append(row); save()
            stdout, stderr = DATA/(PREFIX+'-'+name+'.stdout.log'), DATA/(PREFIX+'-'+name+'.stderr.log')
            with stdout.open('xb') as out, stderr.open('xb') as err:
                child = subprocess.Popen(row['command'], cwd=ROOT, env=env, stdout=out, stderr=err,
                                         stdin=subprocess.DEVNULL, creationflags=subprocess.CREATE_NO_WINDOW)
                row['pid'] = child.pid
                try: row['exit_code'] = child.wait(timeout=120)
                except subprocess.TimeoutExpired:
                    row['timeout'] = True
                    subprocess.run(['taskkill','/F','/T','/PID',str(child.pid)], capture_output=True, timeout=15)
                    if child.poll() is None: child.kill()
                    row['exit_code'] = child.wait(timeout=15)
            for stream,path in (('stdout',stdout),('stderr',stderr)):
                row[stream+'_log'], row[stream+'_sha256'] = path.name, sha(path)
                pins[str(path)] = sha(path)
            row['output_matches_expected'] = stdout.read_bytes().replace(b'\r\n',b'\n') == (ROOT/expected).read_bytes().replace(b'\r\n',b'\n')
            row['passed'] = row['exit_code'] == 0 and not row['timeout'] and stderr.read_bytes() == b'' and row['output_matches_expected']
            save(); assert row['passed'] and stable(), name
            print(name, 'PASS', flush=True)
        record['status'] = 'targeted_correctness_passed'
    except BaseException as error:
        record.update(status='targeted_correctness_failed', error=type(error).__name__+': '+str(error))
    finally:
        record.update(terminal=True, hashes_unchanged=stable(), completed_utc=datetime.now(timezone.utc).isoformat())
        if not record['hashes_unchanged']: record['status'] = 'terminal_invalid_hash_drift'
        save()
    return 0 if record['status'] == 'targeted_correctness_passed' else 1

if __name__ == '__main__': raise SystemExit(main())
