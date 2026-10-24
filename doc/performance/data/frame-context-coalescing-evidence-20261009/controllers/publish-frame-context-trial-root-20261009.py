"""Publish rejected trial and verified GC reproduction; stage no engine changes."""
import csv
import hashlib
import io
import json
from pathlib import Path
import shutil
import statistics
import subprocess

ROOT = Path('D:/CantorAI/xlang3')
DATA = ROOT / 'doc/performance/data'
HEAD = '95feaff27c2a42379f4bdaade27dc6afd13334d0'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
read = lambda p: json.loads(p.read_bytes())
RESTORE = DATA / 'frame-context-coalescing-rejected-restored-20261009.json'
assert sha(RESTORE) == '69d0a50979c12f8cdb31d6f62e8ca625345e1ead6f2b7fbf10c7802e533668e4'
restored = read(RESTORE)
assert restored['terminal'] and not restored['engine_change_retained']
assert all(sha(ROOT / p) == h for p, h in restored['restored_source_sha256'].items())
assert all(sha(ROOT / 'build-repro/main-verify-20261006/Release' / p) == h for p, h in restored['restored_release_sha256'].items())
assert all(sha(ROOT / 'build-repro/Release' / p) == h for p, h in restored['fixed_baseline_sha256'].items())
assert all(sha(ROOT / p) == h for p, h in restored['unowned_tracked_dirty_sha256'].items())
assert subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip() == HEAD
assert not subprocess.check_output(['git', 'diff', '--cached', '--name-only'], cwd=ROOT)
GC = DATA / 'gc-unreachable-instance-cycle-untimed-20261009.json'
assert sha(GC) == '9230878545e8f36a7c73794dcf3e6ca6f5a33cd4071dd808b21d3e64bb4f0983'
gc = read(GC)
assert gc['status'] == 'unreachable_cycle_discovery_failure_reproduced' and gc['hashes_unchanged']
diagnostic = read(DATA / 'frame-context-coalescing-diagnostic-20261009.json')
fast = read(DATA / 'frame-context-coalescing-official-20261009.json')
rigorous = read(DATA / 'frame-context-coalescing-official-rigorous-20261009.json')
broad = read(DATA / 'frame-context-coalescing-call-heavy-official-20261009.json')
for name, value in restored['measurements_sha256'].items():
    assert sha(DATA / name) == value
    doc = read(DATA / name)
    assert all(p['exit_code'] == 0 and p['measurement_valid'] and not p['external_process_watch']['overlaps'] and not p['external_process_watch']['scanner_errors'] for p in doc['phases'])
    for phase in doc['phases']:
        for key in ('stdout', 'stderr'):
            assert sha(DATA / phase[key]) == phase[key + '_sha256']
        watch = phase['external_process_watch']
        assert sha(DATA / watch['log']) == watch['sha256']
        if 'output' in phase:
            assert sha(DATA / phase['output']) == phase['output_sha256']

comparisons = []
for label, doc, scored in (('unpickle-rigorous', rigorous, False), ('call-heavy-fast', broad, True)):
    for pair, (first, second) in enumerate(((0, 1), (3, 2))):
        control, candidate = (DATA / doc['phases'][index]['output'] for index in (first, second))
        result = subprocess.run(['C:/Python/Python314/python.exe', '-m', 'pyperf', 'compare_to', str(control), str(candidate), '--table'], cwd=ROOT, capture_output=True, check=True)
        log = DATA / ('frame-context-coalescing-' + label + '-compare-pair-%d-20261009.txt' % pair)
        assert not log.exists()
        log.write_bytes(result.stdout + result.stderr)
        assert b'hidden because not significant' in result.stdout
        comparisons.append({'label': label, 'pair': pair, 'log': log.name, 'sha256': sha(log), 'exit_code': result.returncode})

rows = []
for phase in diagnostic['phases']:
    for case, values in phase['samples'].items():
        rows.extend({'capture': 'diagnostic', 'phase': phase['name'], 'runtime': phase['runtime'], 'case': case,
            'sample': index, 'time_seconds': value, 'normalization': 'complete checked body; 50000 input items'} for index, value in enumerate(values))
for label, doc in (('official-fast', fast), ('official-rigorous', rigorous), ('call-heavy-fast', broad)):
    for phase in doc['phases']:
        scores = phase.get('scores', {'unpickle_pure_python': {'values': phase.get('values', [])}})
        for case, score in scores.items():
            rows.extend({'capture': label, 'phase': phase['name'], 'runtime': phase['runtime'], 'case': case,
                'sample': index, 'time_seconds': value, 'normalization': 'pyperf normalized scored value; see original metadata/inner_loops'} for index, value in enumerate(score['values']))
assert len(rows) == 1070
csv_path = DATA / 'frame-context-coalescing-all-timing-values-20261009.csv'
assert not csv_path.exists()
with csv_path.open('w', encoding='utf-8', newline='') as stream:
    writer = csv.DictWriter(stream, fieldnames=list(rows[0])); writer.writeheader(); writer.writerows(rows)

evidence = DATA / 'frame-context-coalescing-evidence-20261009'
assert not evidence.exists()
controllers = ['apply-frame-context-coalescing-root-20261009.py', 'build-frame-context-coalescing-root-20261009.py',
    'check-frame-context-coalescing-correctness-root-20261009.py', 'measure-frame-context-coalescing-root-20261009.py',
    'measure-frame-context-coalescing-rigorous-root-20261009.py', 'measure-frame-context-coalescing-call-heavy-root-20261009.py',
    'restore-frame-context-coalescing-root-20261009.py', 'publish-frame-context-trial-root-20261009.py',
    'gc-unreachable-instance-cycle-child-20261009.py', 'check-gc-unreachable-instance-cycle-root-20261009.py',
    'count-original-unpickle-calls-child-20261009.py', 'count-original-unpickle-calls-root-20261009.py',
    'validate-call-ex-cross-activation-constructor-resume-r3-20261008.py', 'build-python-new-vm-continuation-r4-root-20261009.cmd',
    'runtime-frame-context-coalesced-proposed-20261008.patch', 'runtime-frame-context-coalesced-proposed-20261008-provenance.json',
    'cpython3147-branch-call-entry-source-comparison-r2-20261008.md', 'gc-workload-contract-followup-20261007.md']
for name in controllers:
    target = evidence / 'controllers' / name
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(ROOT / 'scratch/performance' / name, target)
shutil.copyfile(ROOT / 'benchmarks/diagnostics/python_callback_boundary.py', evidence / 'python_callback_boundary.py')
counts_path = DATA / 'original-unpickle-call-counts-20261009.json'
assert sha(counts_path) == '4c91fc87fdc53ca4ebcd72ff8a6f2cf6bf1770972b696b7e11464a6c414f84b3'

diagnostic_table = []
for case in ('loop', 'small_calls', 'branch_calls', 'sort_plain', 'sort_callback'):
    cp, control, candidate = (diagnostic['medians_seconds'][runtime][case] for runtime in ('cpython3147', 'control', 'candidate'))
    diagnostic_table.append(f'| `{case}` | {cp * 1000:.4f} | {control * 1000:.4f} | {candidate * 1000:.4f} | {control / candidate:.4f}× | {cp / candidate:.4f}× |')
official_table = []
for label, doc in (('unpickle fast', fast), ('unpickle rigorous', rigorous)):
    official_table.append(f"| {label} | {doc['control_over_candidate_by_pair'][0]:.5f}× | {doc['control_over_candidate_by_pair'][1]:.5f}× |")
for case, ratios in broad['control_over_candidate_by_pair'].items():
    official_table.append(f'| `{case}` fast | {ratios[0]:.5f}× | {ratios[1]:.5f}× |')
report = ROOT / 'doc/performance/frame-context-coalescing-rejected-and-gc-discovery-20261009.md'
assert not report.exists()
text = '''# Frame-context trial rejected; generic GC discovery reproduced

The frame-context optimization was removed. It improves a branching Python-call diagnostic, but it does not produce a significant gain in rigorous official pure-Python unpickling or the three preselected call-heavy official cases. The exact accepted source137 and Release178 from main `95feaff2` have been restored at the existing run path. No new engine optimization is committed in this checkpoint.

## Mechanism and correctness

The held proposal combines three exported Runtime setters and their cached TLS guards at ordinary VM frame switches. It uses the freshly published top physical frame view only when the owned globals Object is unchanged, updates borrowed locals and identity fields in their previous order, and keeps the original changed-owner setter/finalizer sequence as fallback. It does not change IR shapes, remove inspection publication, reuse owning frame snapshots, or replace Python library bodies. Code comments and meaningful ownership/admission tests are preserved in the rejected source archive.

This is distinct from rejected CurrentGlobalsGuard save/restore, callee-module owner selection, and R6 materialized snapshot reuse. Source review found no concrete safety blocker. Fresh validation passed the CPython 3.14.7 four-group oracle, the matching XLang3 result, 406 core fixtures, 11 compatibility sections, three expected-failure checks, nine selected CTests and both SQLite API checks. The full fixed performance gate was not run for this discarded candidate. Its correctness pass is not an acceptance or speed result; the restored accepted binary retains its previously recorded complete default11 gate pass.

## Balanced diagnostic

The unchanged `python_callback_boundary.py` checks outputs and runs three samples per body over 50000 items. All six permutations of CPython/control/candidate ran serially: 18 processes, 270 timed body values. The table uses the median of all 18 values per runtime/body. These are complete body times, including loop or sorting work; they are not exclusive function-entry CPU costs or official scores.

| Body, ms | CPython 3.14.7 | Accepted X control | Trial X | Trial speed / control | Trial speed / CP |
|---|---:|---:|---:|---:|---:|
DIAGNOSTIC_TABLE

The branching call body improves by about 1.088× against the accepted X control, while remaining about 6.8× longer than CPython. That local result does not predict a library or whole-suite speedup.

## Original official cases

All original benchmark definitions and workloads are unchanged. The same CPython 3.14.7 manager, XLang3 Release workers, compatibility hook and dependency site are used. Each four-score sequence is control/candidate/candidate/control. Fast mode retains 20 scored values per case/score; rigorous unpickling retains 120. Warmups/calibration are retained in the original pyperf JSON but excluded from scored values.

The ratio below is control time / candidate time: above 1× favors the candidate, below 1× favors the control. Pair 2 reverses execution order. Every pyperf comparison for rigorous unpickling and the three fast call-heavy cases is hidden as statistically insignificant. DeltaBlue's descriptive means are slower in both orders; Richards changes direction. No official speed gain is claimed.

| Case | Pair 1 speed / control | Pair 2 speed / control |
|---|---:|---:|
OFFICIAL_TABLE

No additional run was used to search for a favorable score. The initial fast unpickle screen justified a rigorous check; its apparent small gain did not survive that check. All 1070 scored timing values are in [CSV](data/frame-context-coalescing-all-timing-values-20261009.csv), together with all original JSON, warnings and logs. The previously published full97 comparison remains its separate 75-completed/22-failed capture; this experiment does not replace its chart or aggregate.

## Confirmed next failure: unreachable cycles

A separate, untimed CP-first reproduction creates a reachable `Node` self-cycle and an abandoned `Node` self-cycle. No weakrefs or finalizers seed the collector; only the abandoned node's integer ID is retained. After collection, both runtimes preserve the reachable cycle. CPython 3.14.7 returns 1 and the abandoned node is absent from `gc.get_objects()`. The accepted XLang3 returns 0 and still lists that node, then fails the identical assertion. [Actual receipt](data/gc-unreachable-instance-cycle-untimed-20261009.json).

Current `gc.collect` delegates to `weakref_collect_cycles`. That collector starts from selected weakref/native/local-class candidates, excludes module-rooted classes, and does not seed the general tracked instance/list heap. The generic tracked-object snapshot is used by object inspection but not collection. The minimal result corroborates the retained source diagnosis and explains why the official `gc_collect` requirement cannot be dismissed as harness noise. It is not a performance score, and repairing this discovery gap must still preserve external roots, native references, finalizers and reentry. No collection-count workaround or GC engine change is included here.

The full97 report already withholds `gc_traversal` ratios because its zero-count assertion cannot establish equivalent collection work while discovery is incomplete. That exclusion remains; the next repair must validate the original collection and traversal benchmarks.

## Earlier unpickle call counts

The separately preserved accepted-build profile confirms 20200 `pickle._Unframer.read` Python calls in one original body, matching the September30 diagnosis; the retained BytesIO fast-call adapter is already present and was not reimplemented. The original body performs 60 loads with official `inner_loops=20` normalization. Profiled Python counts are 33385 for CPython and 33427 for X; profiling disables optimizations and does not attribute unprofiled CPU time. X native profile arguments are often name strings, which this collector did not decode, so its unsupported native-name bucket is not comparable with CPython's callable-object native counts. [Diagnostic receipt](data/original-unpickle-call-counts-20261009.json).

## Provenance and limits

The accepted checkpoint was copied once before edits; source140/Release178 of the rejected trial were preserved before restoring source137/Release178. Both complete Release maps, the fixed baseline177 and protected unrelated working files match their receipts. The restoration copied exact preserved bytes to the original directory; it was not a new build. Trial object files remain in the existing Ninja directory; restored source mtimes require rebuilding before a future candidate is timed. The selected source inventory is partial worktree provenance, not proof of a clean-checkout reproduction.

Every timing phase has pre/post idle checks, one-second compiler/tool observations, raw observations, no recorded overlap/scanner errors and stable input hashes. Sub-second processes can escape sampling; this does not claim an otherwise unused computer. Official rigorous and broad screens pin the original benchmark/Python source inputs. The initial fast unpickle screen lacks those extra source pins, so it is retained only as an exploratory screen, not acceptance evidence. A brief untimed pyperf comparison of the completed rigorous results was also issued after the broad manager launched; it is not another workload measurement or proof of fully exclusive CPU use.

Exact rejected owned source files are archived under `data/frame-context-coalescing-trial-sources-20261009`; controllers, the held proposal/history and unchanged diagnostic source are under `data/frame-context-coalescing-evidence-20261009`. Historical scratch paths inside controllers refer to the original workspace; source/binary hashes and raw receipts remain authoritative. A future rerun requires new output names and current provenance, not blindly replaying a historical controller.

The preceding goal turn was a status clarification and is classified as no progress. This turn applied and measured a distinct held trial, rejected it based on official evidence, restored the accepted checkpoint, and reproduced a concrete remaining GC failure. The overall goal of materially reducing full-suite slowdowns/failures versus CPython remains active and unfinished.
'''
report.write_text(text.replace('DIAGNOSTIC_TABLE', '\n'.join(diagnostic_table)).replace('OFFICIAL_TABLE', '\n'.join(official_table)), encoding='utf-8', newline='\n')

owned = [report]
for pattern in ('frame-context-coalescing-*', 'gc-unreachable-instance-cycle-untimed-20261009*', 'original-unpickle-call-counts-20261009*'):
    for p in DATA.glob(pattern):
        owned.extend(p.rglob('*') if p.is_dir() else [p])
owned = sorted(set(p for p in owned if p.is_file()))
attributes = ROOT / '.gitattributes'
working_before = attributes.read_bytes()
head_attributes = subprocess.check_output(['git', 'show', HEAD + ':.gitattributes'], cwd=ROOT)
rules = b'\n/doc/performance/data/frame-context-coalescing-* -text\n/doc/performance/data/frame-context-coalescing-trial-sources-20261009/** -text\n/doc/performance/data/frame-context-coalescing-evidence-20261009/** -text\n/doc/performance/data/gc-unreachable-instance-cycle-untimed-20261009* -text\n/doc/performance/data/original-unpickle-call-counts-20261009* -text\n'
assert b'/doc/performance/data/frame-context-coalescing-* -text' not in head_attributes
attributes.write_bytes(working_before + rules)
publication = DATA / 'frame-context-coalescing-publication-20261009.json'
assert not publication.exists()
manifest = {'terminal': True, 'status': 'docs_only_ready_to_commit', 'head_before': HEAD,
    'engine_change_retained': False, 'engine_gate_for_trial_run': False,
    'artifact_sha256': {p.relative_to(ROOT).as_posix(): sha(p) for p in owned},
    'comparisons': comparisons, 'timing_value_count': len(rows),
    'working_attributes_before_sha256': hashlib.sha256(working_before).hexdigest(),
    'working_attributes_after_sha256': sha(attributes),
    'staged_attributes_sha256': hashlib.sha256(head_attributes + rules).hexdigest(),
    'controller_sha256': sha(__file__), 'restoration_sha256': sha(RESTORE), 'gc_reproduction_sha256': sha(GC)}
publication.write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8', newline='\n')
owned.append(publication)
temporary_index = ROOT / 'scratch/performance/frame-context-publication-20261009.index'
assert not temporary_index.exists()
import os
env = os.environ.copy(); env['GIT_INDEX_FILE'] = str(temporary_index)
subprocess.run(['git', 'read-tree', HEAD], cwd=ROOT, env=env, check=True)
entries = []
for p in owned + [attributes]:
    content = head_attributes + rules if p == attributes else p.read_bytes()
    oid = subprocess.check_output(['git', 'hash-object', '-w', '--no-filters', '--stdin'], input=content, cwd=ROOT).strip().decode()
    entries.append('100644 ' + oid + '\t' + p.relative_to(ROOT).as_posix() + '\n')
subprocess.run(['git', 'update-index', '--index-info'], input=''.join(entries).encode(), cwd=ROOT, env=env, check=True)
paths = subprocess.check_output(['git', 'diff', '--cached', '--name-only'], cwd=ROOT, env=env, text=True).splitlines()
assert set(paths) == {p.relative_to(ROOT).as_posix() for p in owned + [attributes]}
assert all(p == '.gitattributes' or p.startswith('doc/performance/') for p in paths)
assert all(sha(ROOT / p) == h for p, h in restored['restored_source_sha256'].items())
assert all(sha(ROOT / p) == h for p, h in restored['unowned_tracked_dirty_sha256'].items() if p != '.gitattributes')
assert attributes.read_bytes().startswith(working_before)
assert not subprocess.check_output(['git', 'diff', '--cached', '--name-only'], cwd=ROOT)
index_name = subprocess.check_output(['git', 'rev-parse', '--git-path', 'index'], cwd=ROOT, text=True).strip()
index_path = Path(index_name)
if not index_path.is_absolute(): index_path = ROOT / index_path
shutil.copyfile(temporary_index, index_path)
temporary_index.unlink()
print(json.dumps({'status': 'docs_only_staged', 'paths': len(paths), 'publication_sha256': sha(publication), 'report': str(report)}, indent=2))
