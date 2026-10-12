$ErrorActionPreference = 'Stop'
$repo = 'D:\CantorAI\xlang3'
Set-Location -LiteralPath $repo
$utf8 = [System.Text.UTF8Encoding]::new($false)
$base = 'scratch/performance/inherited-slot-proof-proposal-20261008'
$patchRel = $base + '.patch'
$logRel = $base + '-apply-check.log'
$provenanceRel = $base + '-provenance.json'
$targetNames = @(
 'src/internal/xlang3/object_model.h', 'src/runtime/object_model.cpp',
 'src/builtins/object_type_builtins.cpp', 'src/executor/xlang_vm/ops/xlang_vm_ops_construct.h',
 'src/executor/xlang_vm/xlang_vm_inline_support.h', 'src/executor/xlang_vm/xlang_vm_attr.cpp',
 'src/serialize/value_graph_reader.cpp', 'src/runtime/modules/system/weakref_module.cpp',
 'tests/cpp/canonical_slot_read_cases.h', 'tests/fixtures/core/canonical_slot_reads.py',
 'tests/fixtures/expected/canonical_slot_reads.out')
$sources = @{}
$candidates = @{}
$before = @{}
foreach ($fresh in @($patchRel, $logRel, $provenanceRel, ($base + '-sources'), ($base + '-candidates'))) {
 if (Test-Path -LiteralPath $fresh) { throw "Artifact already exists: $fresh" }
}
$head = (& git rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $head -ne 'e2a752f62227ef1088236c68d6a08e480323e112') { throw 'Accepted HEAD differs' }
foreach ($name in $targetNames) {
 $before[$name] = (Get-FileHash -Algorithm SHA256 -LiteralPath $name).Hash.ToLowerInvariant()
 $copy = $base + '-sources/' + $name
 $candidate = $base + '-candidates/' + $name
 [System.IO.Directory]::CreateDirectory((Join-Path $repo (Split-Path $copy))) | Out-Null
 [System.IO.Directory]::CreateDirectory((Join-Path $repo (Split-Path $candidate))) | Out-Null
 Copy-Item -LiteralPath $name -Destination $copy
 $sources[$name] = $copy
 $candidates[$name] = [System.IO.File]::ReadAllText((Join-Path $repo $name), $utf8).Replace("`r`n", "`n")
}
function R([string] $body, [string] $old, [string] $new) {
 $old = $old.Replace("`r`n", "`n")
 $new = $new.Replace("`r`n", "`n")
 $at = $body.IndexOf($old, [System.StringComparison]::Ordinal)
 if ($at -lt 0 -or $body.IndexOf($old, $at + $old.Length, [System.StringComparison]::Ordinal) -ge 0) { throw ('Nonunique replacement: ' + $old.Substring(0, [Math]::Min(80, $old.Length))) }
 return $body.Remove($at, $old.Length).Insert($at, $new)
}
function Snippet([string] $name) { return [System.IO.File]::ReadAllText((Join-Path $repo $name), $utf8).Replace("`r`n", "`n") }
$name = 'src/internal/xlang3/object_model.h'
$candidates[$name] = R $candidates[$name] '  std::unordered_map<std::string, uint32_t> instance_slot_indices;' @'
  std::unordered_map<std::string, uint32_t> instance_slot_indices;
  // Keep actual own declaration occurrences before inherited layout dedup.
  // Inherited LOAD_ATTR can prove one canonical declaration only from this
  // immutable history; mutable __slots__/attrs and flattened names cannot.
  std::vector<std::string> own_instance_slot_declarations;
  bool own_instance_slot_declarations_known = false;
'@
$candidates[$name] = R $candidates[$name] 'bool class_set_base(Value klass, Value base, std::string& error);' @'
bool class_set_base(Value klass, Value base, std::string& error);
// Only Python construction keeps captured declaration history through base
// assembly. SDK/native mutation retains the original entry and becomes unknown.
bool class_set_base_for_construction(Value klass, Value base, std::string& error);
// Cold layout writers invalidate descendant guards before losing their proof.
void class_forget_slot_declarations(ClassObject* klass);
'@
$name = 'src/runtime/object_model.cpp'
$body = $candidates[$name]
$collectorAt = $body.IndexOf('bool collect_slot_names_from_value(', [System.StringComparison]::Ordinal)
$collectorEnd = $body.IndexOf('void inherit_special_attr_flags(', $collectorAt, [System.StringComparison]::Ordinal)
if ($collectorAt -lt 0 -or $collectorEnd -le $collectorAt) { throw 'Collector range missing' }
$collector = $body.Substring($collectorAt, $collectorEnd - $collectorAt)
$collector = R $collector '    std::string_view private_class_name = {}) {' @'
    std::string_view private_class_name = {},
    std::vector<std::string>* own_declarations = nullptr) {
'@
$collector = R $collector @'
      add_unique_slot_name(
          slots,
          private_class_name.empty() ? name : mangle_instance_slot_name(private_class_name, name));
'@ @'
      const auto normalized = private_class_name.empty()
          ? name : mangle_instance_slot_name(private_class_name, name);
      if (own_declarations != nullptr) own_declarations->push_back(normalized);
      add_unique_slot_name(slots, normalized);
'@
$collector = $collector.Replace('allow_weakref, private_class_name))', 'allow_weakref, private_class_name, own_declarations))')
$body = $body.Remove($collectorAt, $collectorEnd - $collectorAt).Insert($collectorAt, $collector)
$body = R $body '  bool has_explicit_slots = false;' @'
  obj->own_instance_slot_declarations_known = true;
  bool has_explicit_slots = false;
'@
$body = R $body @'
      collect_slot_names_from_value(
          attr.second, obj->instance_slot_names, obj->allow_instance_dict, obj->allow_weakref, obj->name);
'@ @'
      if (!collect_slot_names_from_value(
              attr.second, obj->instance_slot_names, obj->allow_instance_dict,
              obj->allow_weakref, obj->name, &obj->own_instance_slot_declarations)) {
        obj->own_instance_slot_declarations_known = false;
      }
'@
$body = R $body '      add_unique_slot_name(obj->instance_slot_names, slot);' @'
      // Explicit __slots__ has already recorded these compiler-provided names.
      // Native/compiler own layouts without __slots__ retain every occurrence.
      if (!has_explicit_slots && slot != "__dict__" && slot != "__weakref__")
        obj->own_instance_slot_declarations.push_back(slot);
      add_unique_slot_name(obj->instance_slot_names, slot);
'@
$body = R $body @'
void slot_descriptor_set_owner_class(Value& descriptor, const Value& owner_class) {
  if (auto* slot = value_as_slot_descriptor(descriptor)) {
    value_assign_fast(slot->owner_class, owner_class);
  }
}
'@ @'
void slot_descriptor_set_owner_class(Value& descriptor, const Value& owner_class) {
  if (auto* slot = value_as_slot_descriptor(descriptor)) {
    auto* previous = value_as_class(slot->owner_class);
    auto* next = value_as_class(owner_class);
    if (previous != nullptr && previous != next) {
      // Fresh descriptors have no existing proof. Rebinding a live shared
      // descriptor must invalidate all inherited caches before old-owner release.
      class_forget_slot_declarations(previous);
      class_forget_slot_declarations(next);
    }
    value_assign_fast(slot->owner_class, owner_class);
  }
}

void class_forget_slot_declarations(ClassObject* klass) {
  if (klass == nullptr) return;
  klass->own_instance_slot_declarations_known = false;
  invalidate_class_lookup_caches(klass);
}
'@
$body = R $body 'bool class_set_base(Value klass, Value base, std::string& error) {' @'
static bool class_set_base_impl(
    Value klass, Value base, std::string& error, bool construction) {
'@
$body = R $body @'
  std::vector<std::string> own_slots;
  for (const auto& attr : klass_obj->attrs) {
'@ @'
  // SDK/native base mutation cannot certify the historical layout. Python
  // construction alone preserves metadata captured before descriptor dedup.
  if (!construction) klass_obj->own_instance_slot_declarations_known = false;
  invalidate_class_lookup_caches(klass_obj);
  std::vector<std::string> own_slots;
  for (const auto& attr : klass_obj->attrs) {
'@
$body = R $body @'
  class_register_subclass(added_base_class, klass_obj);
  klass_obj->version = next_class_version_tag();
  return true;
}

bool class_get_subclasses
'@ @'
  class_register_subclass(added_base_class, klass_obj);
  invalidate_class_lookup_caches(klass_obj);
  return true;
}

bool class_set_base(Value klass, Value base, std::string& error) {
  return class_set_base_impl(std::move(klass), std::move(base), error, false);
}

bool class_set_base_for_construction(Value klass, Value base, std::string& error) {
  return class_set_base_impl(std::move(klass), std::move(base), error, true);
}

bool class_get_subclasses
'@
$candidates[$name] = $body
$name = 'src/builtins/object_type_builtins.cpp'
$candidates[$name] = R $candidates[$name] '  const size_t existing = klass->instance_slot_names.size();' @'
  if (slots.size() > klass->instance_slot_names.size()) {
    // A metaclass-produced extension is not captured own declaration history.
    class_forget_slot_declarations(klass);
  }
  const size_t existing = klass->instance_slot_names.size();
'@
$candidates[$name] = R $candidates[$name] 'if (!class_set_base(out, resolved_bases[i], error))' 'if (!class_set_base_for_construction(out, resolved_bases[i], error))'
$name = 'src/executor/xlang_vm/ops/xlang_vm_ops_construct.h'
$candidates[$name] = R $candidates[$name] 'if (!class_set_base(regs[in.dst], regs[in.a], error))' 'if (!class_set_base_for_construction(regs[in.dst], regs[in.a], error))'
$name = 'src/executor/xlang_vm/xlang_vm_inline_support.h'
$candidates[$name] = R $candidates[$name] 'if (!class_set_base(out, bases->items[i], error))' 'if (!class_set_base_for_construction(out, bases->items[i], error))'
$name = 'src/serialize/value_graph_reader.cpp'
$candidates[$name] = R $candidates[$name] @'
        auto* c = value_as_class(v);
        c->base = ref(0); c->metaclass = ref(1); c->attrs.clear();
'@ @'
        auto* c = value_as_class(v);
        // Versions 1-4 preserve flattened layout, not original own declarations.
        // The empty construction placeholder is not proof for restored history.
        c->own_instance_slot_declarations_known = false;
        c->own_instance_slot_declarations.clear();
        c->base = ref(0); c->metaclass = ref(1); c->attrs.clear();
'@
$name = 'src/runtime/modules/system/weakref_module.cpp'
$candidates[$name] = R $candidates[$name] '    reference_class->instance_slot_names = {kWeakrefCallbackAttr, kWeakrefHashAttr};' @'
    class_forget_slot_declarations(reference_class);
    reference_class->instance_slot_names = {kWeakrefCallbackAttr, kWeakrefHashAttr};
'@
$name = 'src/executor/xlang_vm/xlang_vm_attr.cpp'
$body = R $candidates[$name] '#include "xlang3/object_model.h"' "#include `"xlang3/object_model.h`"`n#include <algorithm>"
$body = R $body @'
  // Flattened slot metadata can hide duplicate subclass declarations.
  // Until own declarations have durable metadata, inherited slots stay generic.
  if (owner != &klass) return CanonicalSlotPromotion::ShapeIneligible;
'@ '  if (owner == nullptr) return CanonicalSlotPromotion::ShapeIneligible;'
$old = @'
  uint32_t declarations = 0;
  bool exact_owner_seen = false;
  for (const auto& class_value : *mro) {
    auto* candidate = value_as_class(class_value);
    if (candidate == nullptr) return CanonicalSlotPromotion::Retry;
    if (candidate != &klass &&
        candidate->instance_slot_indices.find(name) !=
            candidate->instance_slot_indices.end()) return CanonicalSlotPromotion::ShapeIneligible;
    const auto found = candidate->attrs.find(name);
    if (found == candidate->attrs.end()) continue;
    const auto* declared = value_as_slot_descriptor(found->second);
    if (declared == nullptr ||
        value_as_class(declared->owner_class) != candidate ||
        declared->name != name) continue;
    if (++declarations != 1) return CanonicalSlotPromotion::ShapeIneligible;
    exact_owner_seen = candidate == owner && declared == slot;
  }
  if (!exact_owner_seen || declarations != 1)
    return CanonicalSlotPromotion::ShapeIneligible;
'@
$new = @'
  uint32_t declarations = 0;
  bool exact_owner_seen = false;
  if (owner == &klass) {
    // Preserve the accepted own-only proof and its native/legacy fallbacks.
    for (const auto& class_value : *mro) {
      auto* candidate = value_as_class(class_value);
      if (candidate == nullptr) return CanonicalSlotPromotion::Retry;
      if (candidate != &klass &&
          candidate->instance_slot_indices.find(name) !=
              candidate->instance_slot_indices.end()) return CanonicalSlotPromotion::ShapeIneligible;
      const auto found = candidate->attrs.find(name);
      if (found == candidate->attrs.end()) continue;
      const auto* declared = value_as_slot_descriptor(found->second);
      if (declared == nullptr ||
          value_as_class(declared->owner_class) != candidate ||
          declared->name != name) continue;
      if (++declarations != 1) return CanonicalSlotPromotion::ShapeIneligible;
      exact_owner_seen = candidate == owner && declared == slot;
    }
  } else {
    // Flattened names repeat inherited storage and can hide duplicate own
    // declarations. Prove one immutable own occurrence across the current MRO
    // once, then reuse the existing version/index hit without per-read scans.
    for (const auto& class_value : *mro) {
      auto* candidate = value_as_class(class_value);
      if (candidate == nullptr) return CanonicalSlotPromotion::Retry;
      if (!candidate->own_instance_slot_declarations_known)
        return CanonicalSlotPromotion::ShapeIneligible;
      const auto occurrences = std::count(candidate->own_instance_slot_declarations.begin(),
          candidate->own_instance_slot_declarations.end(), name);
      if (occurrences == 0) continue;
      if (occurrences != 1 || ++declarations != 1)
        return CanonicalSlotPromotion::ShapeIneligible;
      const auto found = candidate->attrs.find(name);
      exact_owner_seen = candidate == owner && found != candidate->attrs.end() &&
          value_as_slot_descriptor(found->second) == slot;
    }
    const auto owner_index = owner->instance_slot_indices.find(name);
    if (owner_index == owner->instance_slot_indices.end() ||
        owner_index->second >= owner->instance_slot_names.size() ||
        owner->instance_slot_names[owner_index->second] != name ||
        owner_index->second != slot->index) return CanonicalSlotPromotion::ShapeIneligible;
  }
  if (!exact_owner_seen || declarations != 1)
    return CanonicalSlotPromotion::ShapeIneligible;
'@
$candidates[$name] = R $body $old $new
$name = 'tests/cpp/canonical_slot_read_cases.h'
$body = R $candidates[$name] '#include <memory>' "#include <memory>`n#include `"serialize/block_stream.h`"`n#include `"serialize/value_graph.h`""
$body = R $body @'
  expect_true(result, xlang_vm_load_attr_cached(inherited_receiver, "x", cache, found, error) &&
      cache.kind == AttrSiteKind::Descriptor && value_is(found, descriptor),
      "initial controlled trial must keep inherited slots generic even when initialized");
'@ @'
  expect_true(result, xlang_vm_load_attr_cached(inherited_receiver, "x", cache, found, error) &&
      cache.kind == AttrSiteKind::InstanceSlot && cache.index == inherited_x && found.as.i64 == 53,
      "known unique inherited declarations install the receiver's effective index");
'@
$body = R $body @'
  cache = AttrSiteCache{};
  for (int repetition = 0; repetition < 3; ++repetition) {
'@ @'
  Value unknown_inherited = Value::class_object("CanonicalSlotUnknownInherited", {}, owner, {"extra"});
  class_forget_slot_declarations(value_as_class(unknown_inherited));
  Value unknown_receiver = Value::instance(unknown_inherited);
  instance_slot_at(value_as_instance(unknown_receiver), inherited_x) = Value::int64(53);
  cache = AttrSiteCache{};
  for (int repetition = 0; repetition < 3; ++repetition) {
'@
$body = R $body '            inherited_receiver, "x", cache, found, error) &&' '            unknown_receiver, "x", cache, found, error) &&'
$body = R $body '"inherited shape rejection remains Descriptor-only and guarded on repeated hits"' '"unknown inherited shape rejection remains Descriptor-only and guarded on repeated hits"'
$body = R $body '"a negative inherited cache must not poison an exact-owner instance at the same site"' '"a negative unknown cache must not poison an exact-owner instance at the same site"'
$body = R $body @'
print('post-warm native hooks and mixed same-class instances retain dispatch: OK')
'@ @'
class InheritedSlotHookOwner(SlotHookOwner):
    __slots__ = ()
third = InheritedSlotHookOwner()
third.x = 31
check_local(third, 31)
_slot_install(third, 331)
check_local(third, 331)
check_getattr(third, 331)
_slot_detach(third)
check_local(third, 31)
print('post-warm native hooks and mixed same-class instances retain dispatch: OK')
'@
$body = R $body 'inline void check_canonical_slot_read_cases(CaseResult& result) {' ((Snippet 'scratch/performance/inherited-slot-proof-CPP-insert-20261008.h') + 'inline void check_canonical_slot_read_cases(CaseResult& result) {')
$body = R $body '  check_canonical_slot_native_hooks(result);' "  check_canonical_slot_native_hooks(result);`n  check_inherited_slot_declaration_proof(result);"
$candidates[$name] = $body
$name = 'tests/fixtures/core/canonical_slot_reads.py'
$body = $candidates[$name]
$at = $body.IndexOf('# Duplicate/shadowed slot declarations', [System.StringComparison]::Ordinal)
$end = $body.IndexOf("assert getattr(base, 'x') is second", $at, [System.StringComparison]::Ordinal)
if ($at -lt 0 -or $end -le $at) { throw 'Old Python note missing' }
$body = $body.Remove($at, $end - $at)
$candidates[$name] = $body + (Snippet 'scratch/performance/inherited-slot-proof-Python-insert-20261008.py')
$name = 'tests/fixtures/expected/canonical_slot_reads.out'
$candidates[$name] += @'
original slot declaration list mutation and deletion preserve live storage: OK
duplicate declarations retain visible-descriptor writes and missing fallback: OK
empty intermediate and added dict layouts retain inherited descriptor precedence: OK
private slot mangling retains inherited storage identity: OK
'@.Replace("`r`n", "`n") + "`n"
function Unified-Diff([string] $oldCopy, [string] $newCopy, [string] $target) {
 $lines = & git diff --no-index --no-ext-diff --no-prefix -- $oldCopy $newCopy
 if ($LASTEXITCODE -ne 1) { throw "Expected changed source diff: $target" }
 for ($index = 0; $index -lt $lines.Count; $index++) {
  if ($lines[$index].StartsWith('diff --git ')) { $lines[$index] = "diff --git a/$target b/$target" }
  elseif ($lines[$index].StartsWith('--- ')) { $lines[$index] = "--- a/$target" }
  elseif ($lines[$index].StartsWith('+++ ')) { $lines[$index] = "+++ b/$target" }
 }
 return ($lines -join "`n") + "`n"
}
$patch = ''
$inventory = @()
foreach ($name in $targetNames) {
 $candidate = $base + '-candidates/' + $name
 [System.IO.File]::WriteAllText((Join-Path $repo $candidate), $candidates[$name], $utf8)
 $patch += Unified-Diff $sources[$name] $candidate $name
 $after = (Get-FileHash -Algorithm SHA256 -LiteralPath $name).Hash.ToLowerInvariant()
 if ($after -ne $before[$name]) { throw "Actual source changed: $name" }
 $inventory += [ordered]@{path=$name;source_sha256_before=$before[$name];source_sha256_after=$after;
   source_copy=$sources[$name];candidate_copy=$candidate;
   candidate_sha256=(Get-FileHash -Algorithm SHA256 -LiteralPath $candidate).Hash.ToLowerInvariant()}
}
[System.IO.File]::WriteAllText((Join-Path $repo $patchRel), $patch, $utf8)
$checkOutput = & git apply --check -- $patchRel 2>&1
$checkExit = $LASTEXITCODE
[System.IO.File]::WriteAllText((Join-Path $repo $logRel), ("command: git apply --check -- $patchRel`nexit: $checkExit`n" + (($checkOutput | ForEach-Object { $_.ToString() }) -join "`n") + "`n"), $utf8)
$artifacts = foreach ($rel in @($patchRel, $logRel, 'scratch/performance/prepare-inherited-slot-proof-20261008.ps1', 'scratch/performance/inherited-slot-proof-CPP-insert-20261008.h', 'scratch/performance/inherited-slot-proof-Python-insert-20261008.py')) {
 [ordered]@{path=$rel;bytes=(Get-Item -LiteralPath $rel).Length;sha256=(Get-FileHash -Algorithm SHA256 -LiteralPath $rel).Hash.ToLowerInvariant()}
}
$record = [ordered]@{status='scratch_only_inherited_proposal_not_applied_not_built_not_run';head=$head;apply_check_exit=$checkExit;targets=@($inventory);artifacts=@($artifacts)}
[System.IO.File]::WriteAllText((Join-Path $repo $provenanceRel), (($record | ConvertTo-Json -Depth 7) + "`n"), $utf8)
if ($checkExit -ne 0) { throw "Inherited proposal failed mechanical apply check: $checkExit" }
Write-Output "Inherited proof proposal checked against accepted $head; 11 actual sources/tests unchanged."
