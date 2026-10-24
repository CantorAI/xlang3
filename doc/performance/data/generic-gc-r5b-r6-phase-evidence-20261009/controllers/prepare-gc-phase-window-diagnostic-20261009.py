"""Use the preserved diagnostic in a new idle window, with automatic exact R4 restoration."""
import hashlib
from pathlib import Path

ROOT = Path('D:/CantorAI/xlang3')
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
original = ROOT / 'scratch/performance/run-gc-phase-idle-diagnostic-20261009.py'
assert sha(original) == '2ad1ce27e45cd62177e59cba6e379d30cacbfcd72cc17a02323d9a21cafb940a'
target = ROOT / 'scratch/performance/run-gc-phase-window-diagnostic-20261009.py'
assert not target.exists()
source = original.read_text().replace('import importlib.util', 'import atexit\nimport shutil\nimport importlib.util', 1)
source = source.replace('phase-diagnostic-idle-', 'phase-diagnostic-window-')
anchor = "assert build['passed'] and build['sources_unchanged']"
activation = '''
r4_state = read(DATA / 'gc-generic-cycles-r4-restored-after-diagnostic-refusals-20261009.json')
assert all(sha(ROOT / p) == h for p, h in r4_state['restored_source_sha256'].items())
assert all(sha(RELEASE / p) == h for p, h in r4_state['restored_release_sha256'].items())
diag_control = ROOT / 'build-repro/controls/gc-phase-diagnostic-ready-20261009'
r4_control = ROOT / 'build-repro/controls/gc-generic-cycles-r4-failed-gate-20261009'
changed_sources = ('src/runtime/modules/system/gc_plain_cycles.h', 'src/runtime/modules/system/gc_module.cpp')
assert all(sha(diag_control / 'sources' / p) == h for p, h in app['source_sha256'].items())
assert all(sha(diag_control / 'Release' / p) == h for p, h in build['release_sha256'].items())
def restore_r4():
    for p in changed_sources: shutil.copyfile(r4_control / 'sources' / p, ROOT / p)
    for p in r4_state['restored_release_sha256']: shutil.copyfile(r4_control / 'Release' / p, RELEASE / p)
    assert all(sha(ROOT / p) == h for p, h in r4_state['restored_source_sha256'].items())
    assert all(sha(RELEASE / p) == h for p, h in r4_state['restored_release_sha256'].items())
    out = DATA / 'gc-generic-cycles-phase-window-restored-r4-20261009.json'
    assert not out.exists()
    out.write_text(json.dumps({'terminal': True, 'status': 'exact_r4_restored_after_phase_window',
        'source_sha256': r4_state['restored_source_sha256'], 'release_sha256': r4_state['restored_release_sha256'],
        'temporary_instrumentation_active': False, 'controller_sha256': sha(__file__)}, indent=2) + '\\n', encoding='utf-8')
    print('Exact R4 source and Release restored; diagnostic instrumentation inactive.', flush=True)
atexit.register(restore_r4)
for p in changed_sources: shutil.copyfile(diag_control / 'sources' / p, ROOT / p)
for p in build['release_sha256']: shutil.copyfile(diag_control / 'Release' / p, RELEASE / p)
'''
assert source.count(anchor) == 1
source = source.replace(anchor, anchor + activation)
target.write_text(source, encoding='utf-8', newline='\n')
print(sha(target))
