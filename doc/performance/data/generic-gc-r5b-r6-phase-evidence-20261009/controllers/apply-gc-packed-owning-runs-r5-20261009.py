"""Use measured edge-scan cost to batch identical owning Values; retain exact counts."""
import hashlib
import json
from pathlib import Path

ROOT = Path('D:/CantorAI/xlang3')
DATA = ROOT / 'doc/performance/data'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
read = lambda p: json.loads(Path(p).read_bytes())
old_app = DATA / 'gc-generic-cycles-applied-source-r4-20261009.json'
app = read(old_app)
r4build = read(DATA / 'gc-generic-cycles-build-r4-20261009.json')
assert all(sha(ROOT / p) == h for p, h in app['source_sha256'].items())
release = ROOT / 'build-repro/main-verify-20261006/Release'
assert all(sha(release / p) == h for p, h in r4build['release_sha256'].items())
profile_path = DATA / 'gc-generic-cycles-phase-diagnostic-window-20261009.json'
profile = read(profile_path)
assert profile['terminal'] and profile['status'] == 'diagnostic_completed'
assert profile['sources_and_releases_unchanged'] and len(profile['phases']) == 6
assert all(p['external_process_watch']['measurement_valid'] for p in profile['phases'])
assert profile['median_phase_us']['plain.owning_edges'] > 900
control = ROOT / 'build-repro/controls/gc-generic-cycles-r4-failed-gate-20261009'
assert all(sha(control / 'sources' / p) == h for p, h in app['source_sha256'].items())
assert all(sha(control / 'Release' / p) == h for p, h in r4build['release_sha256'].items())
# The atexit callback restored/verified all bytes, then failed only while
# computing late __file__ metadata. Independently authenticate restoration now.
restored = DATA / 'gc-generic-cycles-phase-window-restoration-verified-20261009.json'
assert not restored.exists()
restored.write_text(json.dumps({'terminal': True, 'status': 'exact_r4_source_and_release_independently_verified',
    'source_sha256': app['source_sha256'], 'release_sha256': r4build['release_sha256'],
    'profile_receipt_sha256': sha(profile_path), 'temporary_instrumentation_active': False,
    'note': 'Original atexit restored and verified bytes but late __file__ was unavailable when writing its receipt. This independent verification repairs metadata only; no benchmark rerun.'}, indent=2) + '\n', encoding='utf-8')
header = ROOT / 'src/runtime/modules/system/gc_plain_cycles.h'
source = header.read_text()
before = '#include <limits>'
after = '''#include <cstddef>
#if defined(_M_X64) || defined(__x86_64__)
#include <emmintrin.h>
#endif
#include <limits>'''
assert source.count(before) == 1
source = source.replace(before, after)
anchor = '// Repeated owning references still contribute their full multiplicity to'
helper = '''template <class Visit>
void gc_visit_plain_sequence_owned_runs(const std::vector<Value>& items, Visit&& visit) {
  size_t position = 0;
  while (position < items.size()) {
    const Value& first = items[position++];
    if (first.tag != ValueTag::Object || first.as.obj == nullptr ||
        (first.flags & kXlangValueBorrowedRefFlag) != 0) continue;
    Object* target = first.as.obj;
    uint64_t multiplicity = 1;
#if defined(_M_X64) || defined(__x86_64__)
    // x86-64 guarantees SSE2. Compare object representations without copying
    // Values or creating owners. Exact equality includes the borrowed flag;
    // the admitted first Value owns a reference, so every matching lane does.
    // A full four-Value bounds check precedes every unaligned load. Other
    // architectures and differing/tail Values use the same scalar accounting.
    static_assert(sizeof(Value) == 16 && offsetof(Value, tag) == 0 &&
        offsetof(Value, flags) == 4 && offsetof(Value, as) == 8,
        "Packed GC scanning requires the complete 16-byte Value representation");
    const __m128i needle = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&first));
    while (items.size() - position >= 4) {
      const auto* current = items.data() + position;
      const __m128i a = _mm_xor_si128(_mm_loadu_si128(reinterpret_cast<const __m128i*>(current)), needle);
      const __m128i b = _mm_xor_si128(_mm_loadu_si128(reinterpret_cast<const __m128i*>(current + 1)), needle);
      const __m128i c = _mm_xor_si128(_mm_loadu_si128(reinterpret_cast<const __m128i*>(current + 2)), needle);
      const __m128i d = _mm_xor_si128(_mm_loadu_si128(reinterpret_cast<const __m128i*>(current + 3)), needle);
      const __m128i difference = _mm_or_si128(_mm_or_si128(a, b), _mm_or_si128(c, d));
      if (_mm_movemask_epi8(_mm_cmpeq_epi8(difference, _mm_setzero_si128())) != 0xffff) break;
      position += 4;
      multiplicity += 4; // Each equal owning Value counts; this is not deduplication.
    }
#endif
    while (position < items.size() && items[position].tag == ValueTag::Object &&
        items[position].flags == first.flags && items[position].as.obj == target) {
      ++position; ++multiplicity;
    }
    visit(target, multiplicity);
  }
}

'''
assert source.count(anchor) == 1
source = source.replace(anchor, helper + anchor)
anchor = 'void gc_visit_plain_owned_runs(Object* object, Visit&& visit) {\n'
after = anchor + '''  if (object->kind == ObjectKind::List) {
    gc_visit_plain_sequence_owned_runs(reinterpret_cast<ListObject*>(object)->items, visit);
    return;
  }
  if (object->kind == ObjectKind::Tuple) {
    gc_visit_plain_sequence_owned_runs(reinterpret_cast<TupleObject*>(object)->items, visit);
    return;
  }
'''
assert source.count(anchor) == 1
source = source.replace(anchor, after)
header.write_text(source, encoding='utf-8', newline='\n')
tests = ROOT / 'tests/cpp/generic_gc_cases.h'
source = tests.read_text()
anchor = '  Value borrowed_self = Value::list({});'
matrix = '''  // Exercise full packed blocks, short tails and mismatching borrowed/scalar
  // lanes. Reclamation proves multiplicity; a native root proves cached edges
  // still preserve the component before that root is removed.
  for (size_t copies : {size_t{4}, size_t{5}, size_t{8}, size_t{9}, size_t{17}}) {
    for (bool mixed : {false, true}) {
      Value left = Value::list({}), right = Value::list({left});
      auto* left_object = left.as.obj;
      auto* right_object = right.as.obj;
      auto& items = value_as_list(left)->items;
      items.assign(copies, right);
      if (mixed) {
        Value borrowed;
        borrowed.tag = ValueTag::Object; borrowed.flags = kXlangValueBorrowedRefFlag;
        borrowed.as.obj = right_object;
        items.insert(items.begin() + copies / 2, std::move(borrowed));
        items.insert(items.begin() + copies / 2, Value::none());
      }
      value_set_invalid(right);
      expect_true(result, collect(runtime, count) && items.size() == copies + (mixed ? 2 : 0) &&
          value_as_list(items.front()) != nullptr && value_is(value_as_list(items.front())->items.front(), left),
          "packed owning runs preserve descendants reached only through the native root");
      value_set_invalid(left);
      expect_true(result, collect(runtime, count) && count >= 2 &&
          !contains(runtime, left_object, result) && !contains(runtime, right_object, result),
          "packed owning runs and scalar tails count every owner and exclude borrowed lanes");
    }
  }
  for (size_t copies : {size_t{5}, size_t{17}}) {
    Value list = Value::list({});
    Value tuple = Value::tuple(std::vector<Value>(copies, list));
    auto* list_object = list.as.obj;
    auto* tuple_object = tuple.as.obj;
    value_as_list(list)->items.push_back(tuple);
    value_set_invalid(tuple); value_set_invalid(list);
    expect_true(result, collect(runtime, count) && count >= 2 &&
        !contains(runtime, list_object, result) && !contains(runtime, tuple_object, result),
        "packed tuple runs retain exact incoming counts and reclaim a mixed list/tuple cycle");
  }

'''
assert source.count(anchor) == 1
source = source.replace(anchor, matrix + anchor)
tests.write_text(source, encoding='utf-8', newline='\n')
current = {p: sha(ROOT / p) for p in app['source_sha256']}
assert {p for p in current if current[p] != app['source_sha256'][p]} == {
    'src/runtime/modules/system/gc_plain_cycles.h', 'tests/cpp/generic_gc_cases.h'}
assert all(sha(ROOT / p) == h for p, h in app['unowned_tracked_dirty_sha256'].items())
app.update(status='generic_gc_packed_runs_r5_applied_validation_pending', source_sha256=current,
    integration_update={'parent_application_sha256': sha(old_app), 'phase_diagnostic_sha256': sha(profile_path),
        'measured_reason': 'Owning-edge scan median 1012us; snapshot 37.45us and reachability 9.05us. Batch exact equal owning list/tuple Values while preserving multiplicity, borrowed flags and bounds.',
        'temporary_profile_removed': True, 'known_r4_control': str(control),
        'algorithm_changed': True}, controller_sha256=sha(__file__))
out = DATA / 'gc-generic-cycles-applied-source-r5-20261009.json'
assert not out.exists()
out.write_text(json.dumps(app, indent=2) + '\n', encoding='utf-8', newline='\n')
for prefix in ('build-gc-generic-cycles', 'check-gc-generic-cycles-correctness'):
    original = ROOT / ('scratch/performance/' + prefix + '-r4-root-20261009.py')
    target = ROOT / ('scratch/performance/' + prefix + '-r5-root-20261009.py')
    text = original.read_text().replace(sha(old_app), sha(out))
    text = text.replace('applied-source-r4-', 'applied-source-r5-').replace('build-r4-', 'build-r5-')
    text = text.replace('correctness-r4-20261009', 'correctness-r5-20261009')
    assert not target.exists()
    target.write_text(text, encoding='utf-8', newline='\n')
print(json.dumps({'application_sha256': sha(out), 'header_sha256': sha(header), 'cpp_test_sha256': sha(tests)}, indent=2))
