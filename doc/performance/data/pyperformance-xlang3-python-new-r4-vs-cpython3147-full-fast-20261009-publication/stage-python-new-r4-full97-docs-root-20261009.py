"""Independently check raw means/ratios and stage only the publication manifest."""
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

ROOT = next(p for p in Path(__file__).resolve().parents if (p / 'AGENTS.md').is_file())
DATA = ROOT / 'doc/performance/data'
RAW = 'pyperformance-xlang3-python-new-r4-full-fast-r3-20261009'
OUT = 'pyperformance-xlang3-python-new-r4-vs-cpython3147-full-fast-20261009'
HEAD = '5997b264f8b71c38b57c98763788702f38146217'
MANIFEST = DATA / (OUT + '-publication.json')
INITIAL_SHA = '98e8d405ba64c3ceb6521cdf3295b32306fc89c9ea589ae3bd15b4b6f15c44d7'

def sha_bytes(value):
    return hashlib.sha256(value).hexdigest()

def sha(path):
    return sha_bytes(Path(path).read_bytes())

def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))

def git(*args, env=None):
    return subprocess.check_output(['git', *args], cwd=ROOT, env=env)

def means(document):
    result = {}
    for bench in document['benchmarks']:
        metadata = dict(document.get('metadata', {}))
        metadata.update(bench.get('metadata', {}))
        values = [float(v) for run in bench['runs'] for v in run.get('values', [])]
        assert values and all(math.isfinite(v) and v > 0 for v in values)
        result[metadata['name']] = sum(values) / len(values)
    return result

def main():
    assert sys.version_info[:3] == (3, 14, 7)
    assert Path(sys.executable).resolve() == Path('C:/Python/Python314/python.exe').resolve()
    assert sha(MANIFEST) == INITIAL_SHA
    manifest = read(MANIFEST)
    assert git('rev-parse', 'HEAD').decode().strip() == HEAD
    assert not git('diff', '--cached', '--name-only').strip(), 'Preserve preexisting staged work'
    for relative, expected in manifest['files_sha256'].items():
        assert relative.startswith('doc/performance/')
        assert sha(ROOT / relative) == expected, relative
    with (DATA / (OUT + '-all-97-status.csv')).open(encoding='utf-8-sig', newline='') as f:
        statuses = list(csv.DictReader(f))
    assert len(statuses) == len({r['benchmark'] for r in statuses}) == 97
    assert sum(r['XLang3 status'] == 'completed' for r in statuses) == 75
    assert sum(r['XLang3 status'].startswith('failed:') for r in statuses) == 22
    with (DATA / (OUT + '-subtests.csv')).open(encoding='utf-8-sig', newline='') as f:
        records = list(csv.DictReader(f))
    cp = means(read(DATA / 'pyperformance-cpython3147-live-eval-full-fast-20261007.json'))
    xx = means(read(DATA / (RAW + '.json')))
    assert len(records) == 84
    scored = 0
    for row in records:
        name = row['subtest']
        assert math.isclose(float(row['CPython 3.14 seconds']), cp[name], rel_tol=1e-14)
        assert math.isclose(float(row['XLang3 seconds']), xx[name], rel_tol=1e-14)
        speed = row['CPython / XLang3 speedup']
        elapsed = row['XLang3 / CPython elapsed time']
        if name == 'gc_traversal':
            assert not speed and not elapsed and row['result'].startswith('unscored:')
        else:
            assert math.isclose(float(speed), cp[name] / xx[name], rel_tol=1e-14)
            assert math.isclose(float(elapsed), xx[name] / cp[name], rel_tol=1e-14)
            assert math.isclose(float(speed) * float(elapsed), 1, rel_tol=1e-14)
            scored += 1
    assert scored == 83
    svg = ET.parse(ROOT / 'doc/performance' / (OUT + '.svg')).getroot()
    labels = [node.text for node in svg.iter() if node.attrib.get('class') == 'label']
    assert len(labels) == 83 and set(labels) == {r['subtest'] for r in records if r['subtest'] != 'gc_traversal'}
    assert any(node.attrib.get('class') == 'baseline' for node in svg.iter())
    assert len([node for node in svg.iter() if node.attrib.get('class') in ('fast', 'slow')]) == 83
    report = (ROOT / 'doc/performance' / (OUT + '.md')).read_text(encoding='utf-8')
    assert 'unpaired' in report and 'successful scored subset' in report
    assert report.count('| `') == 181, '84 subtests plus 97 definition rows'
    for relative in ('pyperformance-cpython3147-live-eval-full-fast-20261007.json',
                     'pyperformance-cpython3147-live-eval-full-fast-20261007.log',
                     'pyperformance-cpython3147-live-eval-full-fast-20261007-provenance.json',
                     'gc-traversal-coverage-exclusion-20261007.json'):
        git('cat-file', '-e', 'HEAD:doc/performance/data/' + relative)
    bundle = DATA / (OUT + '-publication')
    audit_path = bundle / 'root-independent-artifact-verification.json'
    audit = {'status': 'artifact_verification_passed', 'scope': 'Documentation checks, not engine tests or benchmark reruns.',
        'initial_publication_manifest_sha256': INITIAL_SHA, 'all_97_definition_rows': 97, 'raw_subtest_rows': 84,
        'scored_subtests': 83, 'independent_raw_mean_method': 'sum of timed run.values divided by count; no warmups',
        'speed_elapsed_reciprocals_verified': True, 'svg_names_bars_and_baseline_verified': True,
        'full_markdown_rows_verified': 181, 'saved_reference_files_already_tracked': True,
        'engine_changed': False, 'verifier_sha256': sha(__file__)}
    audit_path.write_text(json.dumps(audit, indent=2) + '\n', encoding='utf-8', newline='\n')
    verifier_copy = bundle / Path(__file__).name
    shutil.copyfile(__file__, verifier_copy)
    for path in (audit_path, verifier_copy):
        manifest['files_sha256'][path.relative_to(ROOT).as_posix()] = sha(path)
    manifest['root_independent_artifact_verification'] = audit_path.relative_to(ROOT).as_posix()
    MANIFEST.write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8', newline='\n')
    owned = dict(manifest['files_sha256'])
    owned[MANIFEST.relative_to(ROOT).as_posix()] = sha(MANIFEST)
    index = Path(git('rev-parse', '--git-path', 'index').decode().strip())
    if not index.is_absolute():
        index = ROOT / index
    before_index = index.read_bytes()
    fd, temporary = tempfile.mkstemp(prefix='full97-docs-index-', dir=ROOT / 'scratch/performance')
    os.close(fd)
    temporary = Path(temporary)
    temporary.unlink()
    env = os.environ.copy()
    env['GIT_INDEX_FILE'] = str(temporary)
    try:
        git('read-tree', 'HEAD', env=env)
        for relative, expected in sorted(owned.items()):
            assert sha(ROOT / relative) == expected
            blob = git('hash-object', '-w', '--no-filters', '--', str(ROOT / relative)).decode().strip()
            git('update-index', '--add', '--cacheinfo', '100644,' + blob + ',' + relative, env=env)
            assert sha_bytes(git('cat-file', 'blob', blob)) == expected
        staged = set(git('diff', '--cached', '--name-only', env=env).decode().splitlines())
        assert staged == set(owned), (staged - set(owned), set(owned) - staged)
        assert git('rev-parse', 'HEAD').decode().strip() == HEAD
        assert index.read_bytes() == before_index
        lock = index.with_name(index.name + '.lock')
        with lock.open('xb') as stream:
            stream.write(temporary.read_bytes())
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(lock, index)
        assert set(git('diff', '--cached', '--name-only').decode().splitlines()) == set(owned)
    finally:
        if temporary.exists():
            temporary.unlink()
    print(json.dumps({'status': 'scoped_publication_staged', 'paths': len(owned),
        'manifest_sha256': sha(MANIFEST), 'engine_changes': False, 'artifact_checks': audit}, indent=2))

if __name__ == '__main__':
    main()
