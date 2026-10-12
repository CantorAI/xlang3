"""Preserve terminal R4, then add temporary phase attribution, never an accepted engine."""
import hashlib
import json
from pathlib import Path
import shutil

ROOT = Path('D:/CantorAI/xlang3')
DATA = ROOT / 'doc/performance/data'
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
read = lambda p: json.loads(Path(p).read_bytes())
old_app = DATA / 'gc-generic-cycles-applied-source-r4-20261009.json'
perf = DATA / 'gc-generic-cycles-performance-r4-activity-20261009.json'
assert sha(old_app) == '8b00bc5a7bb84a4440a32c4e789229e6000434829b70c0c0e08adb3496f45362'
assert sha(perf) == '6e0f108f4381e20364a300534e6c960caf8147695c6ad7ded3d6c620f2e7f6da'
app, result = read(old_app), read(perf)
assert result['terminal'] and result['hashes_unchanged'] and not result['fixed_gate']['passed']
assert all(sha(p) == h for p, h in result['pins_before'].items())
control = ROOT / 'build-repro/controls/gc-generic-cycles-r4-failed-gate-20261009'
assert not control.exists()
control.mkdir()
shutil.copytree(RELEASE, control / 'Release')
for p, h in app['source_sha256'].items():
    target = control / 'sources' / p
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(ROOT / p, target)
    assert sha(target) == h
binaries = read(DATA / 'gc-generic-cycles-build-r4-20261009.json')['release_sha256']
assert all(sha(control / 'Release' / p) == h for p, h in binaries.items())
(control / 'provenance.json').write_text(json.dumps({'accepted_baseline': False,
    'parent_application_sha256': sha(old_app), 'performance_sha256': sha(perf),
    'source_sha256': app['source_sha256'], 'release_sha256': binaries,
    'controller_sha256': sha(__file__)}, indent=2) + '\n', encoding='utf-8')
header = ROOT / 'src/runtime/modules/system/gc_plain_cycles.h'
source = header.read_text()
source = source.replace('#include <limits>', '#include <chrono>\n#include <cstdio>\n#include <limits>')
anchor = 'bool gc_plain_storage_kind(ObjectKind kind) {'
helper = '''// TEMPORARY DIAGNOSTIC ONLY: removed before any accepted engine build.
struct GcPhaseDiagnostic {
  using Clock = std::chrono::steady_clock;
  const char* group;
  Clock::time_point last = Clock::now();
  explicit GcPhaseDiagnostic(const char* name) : group(name) {}
  void mark(const char* phase, uint64_t count = 0) {
    const auto end = Clock::now();
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - last).count();
    std::fprintf(stderr, "XLANG3_GC_PHASE %s %s %lld %llu\\n", group, phase,
        static_cast<long long>(ns), static_cast<unsigned long long>(count));
    last = Clock::now(); // Exclude diagnostic output from the next phase.
  }
};

'''
assert source.count(anchor) == 1
source = source.replace(anchor, helper + anchor)
replacements = {
    '  struct Node {': '  GcPhaseDiagnostic diagnostic("plain");\n  struct Node {',
    '    snapshot = gc_snapshot_tracked_objects();': '    diagnostic.mark("lock");\n    snapshot = gc_snapshot_tracked_objects();\n    diagnostic.mark("snapshot", snapshot.size());',
    '    std::vector<std::vector<size_t>> parents(nodes.size()), children(nodes.size());': '    diagnostic.mark("nodes", nodes.size());\n    std::vector<std::vector<size_t>> parents(nodes.size()), children(nodes.size());',
    '    std::vector<size_t> pending;': '    diagnostic.mark("owning_edges");\n    std::vector<size_t> pending;',
    '    for (size_t i = 0; i < nodes.size(); ++i) {\n      auto& node = nodes[i];': '    diagnostic.mark("unsafe_propagation");\n    for (size_t i = 0; i < nodes.size(); ++i) {\n      auto& node = nodes[i];',
    '    while (!pending.empty()) {\n      const size_t source = pending.back();': '    diagnostic.mark("root_counts", pending.size());\n    while (!pending.empty()) {\n      const size_t source = pending.back();',
    '    size_t retired_count = 0;': '    diagnostic.mark("reachability");\n    size_t retired_count = 0;',
    '    std::vector<Value> retired, candidate_pins;': '    diagnostic.mark("retirement_count", collected);\n    std::vector<Value> retired, candidate_pins;',
    '    candidate_pins.clear(); // Class/opaque snapshot pins still prevent cleanup callbacks.': '    candidate_pins.clear(); // Class/opaque snapshot pins still prevent cleanup callbacks.\n    diagnostic.mark("retirement", collected);',
    '  snapshot.clear(); // No registry lock or cached graph references across cleanup/reentry.': '  diagnostic.mark("graph_teardown");\n  snapshot.clear(); // No registry lock or cached graph references across cleanup/reentry.\n  diagnostic.mark("snapshot_release");'
}
for before, after in replacements.items():
    assert source.count(before) == 1, before
    source = source.replace(before, after)
header.write_text(source, encoding='utf-8', newline='\n')
module = ROOT / 'src/runtime/modules/system/gc_module.cpp'
source = module.read_text()
before = '  uint64_t collected = weakref_collect_cycles(runtime);'
after = '  GcPhaseDiagnostic collection_diagnostic("collect");\n' + before + '\n  collection_diagnostic.mark("specialized", collected);'
assert source.count(before) == 1
source = source.replace(before, after)
before = '  collected += gc_collect_plain_cycles();'
assert source.count(before) == 1
source = source.replace(before, before + '\n  collection_diagnostic.mark("plain_total", collected);')
module.write_text(source, encoding='utf-8', newline='\n')
current = {p: sha(ROOT / p) for p in app['source_sha256']}
changed = {p for p in current if current[p] != app['source_sha256'][p]}
assert changed == {'src/runtime/modules/system/gc_plain_cycles.h', 'src/runtime/modules/system/gc_module.cpp'}
app.update(status='temporary_phase_attribution_not_acceptable_engine', source_sha256=current,
    engine_commit_permitted=False, integration_update={'parent_application_sha256': sha(old_app),
        'terminal_failed_gate_sha256': sha(perf), 'diagnostic_only': True,
        'r4_exact_control': str(control), 'profiled_scores_not_comparable': True}, controller_sha256=sha(__file__))
out = DATA / 'gc-generic-cycles-phase-diagnostic-applied-20261009.json'
assert not out.exists()
out.write_text(json.dumps(app, indent=2) + '\n', encoding='utf-8', newline='\n')
original = ROOT / 'scratch/performance/build-gc-generic-cycles-r4-root-20261009.py'
source = original.read_text().replace(sha(old_app), sha(out))
source = source.replace('gc-generic-cycles-applied-source-r4-20261009', 'gc-generic-cycles-phase-diagnostic-applied-20261009')
source = source.replace('gc-generic-cycles-build-r4-20261009', 'gc-generic-cycles-phase-diagnostic-build-20261009')
target = ROOT / 'scratch/performance/build-gc-phase-diagnostic-20261009.py'
assert not target.exists()
target.write_text(source, encoding='utf-8', newline='\n')
print(json.dumps({'application_sha256': sha(out), 'r4_control': str(control)}, indent=2))
