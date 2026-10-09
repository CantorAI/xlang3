"""Attribute the changed scan, preserving the diagnostic and restoring normal R5b at exit."""
import hashlib
from pathlib import Path

ROOT = Path('D:/CantorAI/xlang3')
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
original = ROOT / 'scratch/performance/run-gc-phase-idle-diagnostic-20261009.py'
assert sha(original) == '2ad1ce27e45cd62177e59cba6e379d30cacbfcd72cc17a02323d9a21cafb940a'
source = original.read_text().replace('import importlib.util', 'import atexit\nimport shutil\nimport importlib.util', 1)
source = source.replace('gc-generic-cycles-phase-diagnostic-applied-20261009', 'gc-generic-cycles-r5b-phase-diagnostic-applied-20261009')
source = source.replace('gc-generic-cycles-phase-diagnostic-build-20261009', 'gc-generic-cycles-r5b-phase-diagnostic-build-20261009')
source = source.replace('e4f795971c0d5a7e42085c3e5081da010c7c031af2f4d35cb13bca47a267007d', '90737b0ecf15e26ac81afaa333893ff21f9b294d009e95b99e37007a0fef2be2')
source = source.replace('4755c35a1ec8b82e4062a23a748c046b7268d193ca16abbeec0c05db8dbd8ffb', '966f04993b0aad4a96a989fe46334e4d3c2e89db2bc0d2cd1afbc68758a82888')
source = source.replace('gc-generic-cycles-phase-diagnostic-idle-', 'gc-generic-cycles-r5b-phase-diagnostic-')
source = source.replace("record['previous_zero_phase_refusal_sha256'] = sha(DATA / 'gc-generic-cycles-phase-diagnostic-20261009.json')",
    "record['r4_phase_reference_sha256'] = sha(DATA / 'gc-generic-cycles-phase-diagnostic-window-20261009.json')")
anchor = "assert build['passed'] and build['sources_unchanged']"
restore = '''
normal = read(DATA / 'gc-generic-cycles-r5b-before-phase-20261009.json')
normal_control = ROOT / 'build-repro/controls/gc-generic-cycles-r5b-failed-gate-20261009'
diag_control = ROOT / 'build-repro/controls/gc-r5b-phase-diagnostic-ready-20261009'
assert all(sha(ROOT / p) == h for p, h in app['source_sha256'].items())
assert all(sha(RELEASE / p) == h for p, h in build['release_sha256'].items())
assert not diag_control.exists()
diag_control.mkdir()
shutil.copytree(RELEASE, diag_control / 'Release')
for p, h in app['source_sha256'].items():
    target = diag_control / 'sources' / p
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(ROOT / p, target)
    assert sha(target) == h
assert all(sha(diag_control / 'Release' / p) == h for p, h in build['release_sha256'].items())
(diag_control / 'provenance.json').write_text(json.dumps({'diagnostic_only': True,
    'source_sha256': app['source_sha256'], 'release_sha256': build['release_sha256'],
    'application_sha256': sha(app_path), 'build_sha256': sha(build_path)}, indent=2) + '\\n', encoding='utf-8')
def restore_normal(controller_hash=sha(__file__)):
    for p in ('src/runtime/modules/system/gc_plain_cycles.h', 'src/runtime/modules/system/gc_module.cpp'):
        shutil.copyfile(normal_control / 'sources' / p, ROOT / p)
    for p in normal['restored_release_sha256']: shutil.copyfile(normal_control / 'Release' / p, RELEASE / p)
    assert all(sha(ROOT / p) == h for p, h in normal['restored_source_sha256'].items())
    assert all(sha(RELEASE / p) == h for p, h in normal['restored_release_sha256'].items())
    out = DATA / 'gc-generic-cycles-r5b-after-phase-restored-20261009.json'
    assert not out.exists()
    out.write_text(json.dumps({'terminal': True, 'status': 'exact_normal_r5b_restored',
        'source_sha256': normal['restored_source_sha256'], 'release_sha256': normal['restored_release_sha256'],
        'temporary_instrumentation_active': False, 'controller_sha256': controller_hash}, indent=2) + '\\n', encoding='utf-8')
    print('Exact normal R5b restored; instrumentation inactive.', flush=True)
atexit.register(restore_normal)
'''
assert source.count(anchor) == 1
source = source.replace(anchor, anchor + restore)
target = ROOT / 'scratch/performance/run-gc-r5b-phase-diagnostic-20261009.py'
assert not target.exists()
target.write_text(source, encoding='utf-8', newline='\n')
print(sha(target))
