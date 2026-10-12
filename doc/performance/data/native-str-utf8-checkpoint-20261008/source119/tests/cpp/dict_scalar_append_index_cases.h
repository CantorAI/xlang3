/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/
#pragma once

#include "test_harness.h"
#include "xlang3/mapping.h"
#include "xlang3/object_model.h"
#include "xlang3/value_hash.h"

#include <algorithm>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace xlang3::test {
namespace dict_scalar_append_cases {

inline bool warm(Runtime& runtime, const Value& dictionary) {
  Value out;
  std::string error;
  // An Int64 miss reaches the runtime buckets even in an all-string dict;
  // a string hit alone would only warm the separate flat string table.
  return !mapping_get_item_runtime(runtime, dictionary, Value::int64(9000000),
                                  out, error, false) && error == "key not found";
}

inline bool bucket_contains(const DictObject& dictionary, const Value& key, size_t index) {
  size_t hash = 0;
  std::string error;
  if (!value_hash_key(key, hash, error)) return false;
  const auto bucket = dictionary.runtime_hash_index.find(static_cast<int64_t>(hash));
  return bucket != dictionary.runtime_hash_index.end() &&
      std::count(bucket->second.begin(), bucket->second.end(), index) == 1;
}

inline bool complete_locations(const DictObject& dictionary) {
  std::vector<size_t> counts(dictionary.entries.size(), 0);
  for (const auto& bucket : dictionary.runtime_hash_index) {
    for (const size_t index : bucket.second) {
      if (index >= counts.size()) return false;
      ++counts[index];
    }
  }
  return std::all_of(counts.begin(), counts.end(), [](size_t count) { return count == 1; });
}

inline bool fresh(const DictObject& dictionary, bool intrinsic) {
  return dictionary.runtime_hash_indexed_entry_count == dictionary.entries.size() &&
      dictionary.intrinsic_hash_checked_entry_count == dictionary.entries.size() &&
      dictionary.intrinsic_hash_keys_only == intrinsic &&
      dictionary.indexed_entry_count == dictionary.entries.size() &&
      complete_locations(dictionary);
}

struct HashAudit { uint32_t calls = 0; };
struct HashContext { std::shared_ptr<HashAudit> audit; };
inline void destroy_hash_context(void* context) { delete static_cast<HashContext*>(context); }
inline bool callback_hash(Runtime&, const Value*, uint32_t argc, Value& out,
                          std::string& error, void* context) {
  if (argc != 1) { error = "scalar append test hash arity"; return false; }
  ++static_cast<HashContext*>(context)->audit->calls;
  out = Value::int64(7000000);
  return true;
}

struct GrowthAudit {
  DictObject* storage = nullptr;
  bool armed = false;
  bool saw_flat_rebuild = false;
};
struct GrowthContext { std::shared_ptr<GrowthAudit> audit; };
inline void destroy_growth_context(void* context) { delete static_cast<GrowthContext*>(context); }
inline bool unused_growth_token(Runtime&, const Value*, uint32_t, Value&,
                                std::string& error, void*) {
  error = "scalar append growth token must not be called";
  return false;
}
inline bool growth_getter(const Value& object, const std::string& name,
                          Value& out, std::string&) {
  if (name != "__xlang3_int_value__") return false;
  auto* instance = value_as_instance(object);
  if (instance == nullptr) return false;
  for (const auto& attribute : instance->attrs) {
    if (attribute.first != "__xlang3_scalar_append_audit__") continue;
    auto* token = value_as_native_function(attribute.second);
    if (token == nullptr) return false;
    auto audit = static_cast<GrowthContext*>(token->user_data)->audit;
    if (audit->armed && audit->storage != nullptr &&
        audit->storage->indexed_entry_count == static_cast<size_t>(-1)) {
      audit->saw_flat_rebuild = true;
      // Mutate only scalar index storage: no entry/value destruction, dangling
      // key references, or dependence on the existing vector-reentry defects.
      audit->storage->runtime_hash_index.clear();
    }
    out = Value::int64(-500);
    return true;
  }
  return false;
}

} // namespace dict_scalar_append_cases

inline void check_dict_scalar_append_index_cases(CaseResult& result) {
  using namespace dict_scalar_append_cases;
  std::ostringstream output;
  Runtime runtime(output);

  // Public DLL mapping calls, not copied VM templates: freshness/buckets are
  // checked BEFORE each get can repair a table, as well as after the read.
  for (bool strings : {false, true}) {
    Value dictionary = Value::dict_reserved(0);
    auto* storage = value_as_dict(dictionary);
    bool ok = warm(runtime, dictionary);
    for (int64_t index = 0; index != 64 && ok; ++index) {
      Value key = strings ? Value::string("key-" + std::to_string(index))
                          : Value::int64(1000 + index);
      std::string error;
      ok = storage->runtime_hash_indexed_entry_count == storage->entries.size() &&
          mapping_set_item(dictionary, key, Value::int64(index), error) && error.empty() &&
          storage->entries.size() == static_cast<size_t>(index + 1) &&
          fresh(*storage, true) && bucket_contains(*storage, key, static_cast<size_t>(index));
      Value out;
      if (ok) ok = mapping_get_item_runtime(runtime, dictionary, key, out, error, false) &&
          error.empty() && out.tag == ValueTag::Int64 && out.as.i64 == index && fresh(*storage, true);
      if (ok) ok = value_key_equal(storage->entries[static_cast<size_t>(index)].first, key);
    }
    expect_true(result, ok, strings
        ? "64 native string appends must extend fresh runtime buckets and preserve insertion order"
        : "64 physical Int64 appends must extend fresh runtime buckets before the next get");
  }

  // Unknown and explicitly stale tables must not be classified or rebuilt by
  // an append. A separately current positive/negative intrinsic proof may be
  // extended while runtime buckets remain stale.
  {
    Value dictionary = Value::dict_reserved(0);
    auto* storage = value_as_dict(dictionary);
    std::string error;
    bool ok = mapping_set_item(dictionary, Value::int64(1), Value::int64(10), error) &&
        storage->runtime_hash_indexed_entry_count != storage->entries.size() &&
        storage->intrinsic_hash_checked_entry_count != storage->entries.size() &&
        storage->runtime_hash_index.empty();
    ok = ok && warm(runtime, dictionary);
    storage->runtime_hash_indexed_entry_count = static_cast<size_t>(-1);
    auto buckets = storage->runtime_hash_index;
    error.clear();
    ok = ok && mapping_set_item(dictionary, Value::int64(2), Value::int64(20), error) &&
        storage->runtime_hash_indexed_entry_count != storage->entries.size() &&
        storage->runtime_hash_index == buckets &&
        storage->intrinsic_hash_checked_entry_count == storage->entries.size() &&
        storage->intrinsic_hash_keys_only;
    storage->intrinsic_hash_checked_entry_count = static_cast<size_t>(-1);
    error.clear();
    ok = ok && mapping_set_item(dictionary, Value::int64(3), Value::int64(30), error) &&
        storage->runtime_hash_indexed_entry_count != storage->entries.size() &&
        storage->intrinsic_hash_checked_entry_count != storage->entries.size() &&
        storage->runtime_hash_index == buckets;
    expect_true(result, ok, "cold/stale scalar append must keep unknown certificates and runtime buckets stale");
  }

  {
    Value dictionary = Value::dict_reserved(0);
    auto* storage = value_as_dict(dictionary);
    std::string error;
    bool ok = mapping_set_item(dictionary, Value::int64(50), Value::int64(4), error) &&
        warm(runtime, dictionary);
    storage->intrinsic_hash_checked_entry_count = static_cast<size_t>(-1);
    error.clear();
    ok = ok && mapping_set_item(dictionary, Value::int64(51), Value::int64(5), error) &&
        storage->runtime_hash_indexed_entry_count == storage->entries.size() &&
        storage->intrinsic_hash_checked_entry_count != storage->entries.size() &&
        !storage->intrinsic_hash_keys_only && bucket_contains(*storage, Value::int64(51), 1);
    expect_true(result, ok, "a fresh runtime table must not manufacture an unknown whole-key-set proof");
  }

  // Integer-like callback keys are an existing flat-index shape; their false
  // intrinsic certificate must stay false, and appending distinct native keys
  // must not rehash the stored callback key between geometric flat rebuilds.
  // Growth is an existing native attribute-hook boundary and remains stale
  // until the ordinary runtime lookup rebuilds it. This does not certify equality
  // semantics for numeric subclasses or repair the existing numeric fallback.
  {
    auto audit = std::make_shared<HashAudit>();
    auto context = std::make_unique<HashContext>();
    context->audit = audit;
    Value hash = runtime.make_native_function("ScalarAppendKey.__hash__", callback_hash,
                                            context.get(), destroy_hash_context);
    context.release();
    Value klass = Value::class_object("ScalarAppendKey", {{"__hash__", hash}});
    Value key = Value::instance(klass);
    std::string error;
    bool ok = object_set_attr(key, "__xlang3_int_value__", Value::int64(-500), error);
    Value dictionary = Value::dict_reserved(0);
    auto* storage = value_as_dict(dictionary);
    ok = ok && mapping_set_item(dictionary, key, Value::int64(7), error) && warm(runtime, dictionary) &&
        storage->intrinsic_hash_checked_entry_count == storage->entries.size() &&
        !storage->intrinsic_hash_keys_only && !storage->index_has_other_keys && audit->calls == 1;
    for (int64_t index = 0; index != 64 && ok; ++index) {
      Value scalar = Value::int64(2000 + index);
      const auto hash_calls = audit->calls;
      const bool flat_growth = storage->integer_index.empty() ||
          storage->entries.size() >= storage->integer_index.size() / 2;
      error.clear();
      ok = mapping_set_item(dictionary, scalar, Value::int64(index), error) && error.empty() &&
          storage->intrinsic_hash_checked_entry_count == storage->entries.size() &&
          !storage->intrinsic_hash_keys_only && audit->calls == hash_calls;
      if (ok) ok = flat_growth
          ? storage->runtime_hash_indexed_entry_count != storage->entries.size()
          : fresh(*storage, false) && bucket_contains(*storage, scalar, static_cast<size_t>(index + 1));
      Value out;
      if (ok) ok = mapping_get_item_runtime(runtime, dictionary, scalar, out, error, false) &&
          out.tag == ValueTag::Int64 && out.as.i64 == index && fresh(*storage, false) &&
          audit->calls == hash_calls + (flat_growth ? 1 : 0) &&
          bucket_contains(*storage, scalar, static_cast<size_t>(index + 1));
    }
    const auto hash_calls = audit->calls;
    storage->runtime_hash_indexed_entry_count = static_cast<size_t>(-1);
    auto buckets = storage->runtime_hash_index;
    error.clear();
    ok = ok && mapping_set_item(dictionary, Value::int64(4000), Value::int64(4), error) &&
        storage->runtime_hash_index == buckets &&
        storage->runtime_hash_indexed_entry_count != storage->entries.size() &&
        storage->intrinsic_hash_checked_entry_count == storage->entries.size() &&
        !storage->intrinsic_hash_keys_only && audit->calls == hash_calls;
    expect_true(result, ok, "scalar append must preserve false proof and rehash callback keys only at flat growth");
  }

  // Non-physical numeric input retains the old setter search, but cannot
  // extend a runtime certificate merely because the flat numeric probe admits it.
  {
    Value dictionary = Value::dict_reserved(0);
    auto* storage = value_as_dict(dictionary);
    std::string error;
    bool ok = warm(runtime, dictionary) &&
        mapping_set_item(dictionary, Value::boolean(true), Value::int64(1), error) &&
        storage->runtime_hash_indexed_entry_count != storage->entries.size() &&
        storage->intrinsic_hash_checked_entry_count != storage->entries.size() &&
        storage->runtime_hash_index.empty();
    expect_true(result, ok, "bool/int-like append must not enter physical Int64 bucket maintenance");
  }

  {
    Value dictionary = Value::dict_reserved(0);
    auto* storage = value_as_dict(dictionary);
    auto growth_audit = std::make_shared<GrowthAudit>();
    struct GrowthGuard {
      std::shared_ptr<GrowthAudit> audit;
      ~GrowthGuard() { audit->armed = false; audit->storage = nullptr; }
    } guard{growth_audit};
    auto growth_context = std::make_unique<GrowthContext>();
    growth_context->audit = growth_audit;
    Value token = runtime.make_native_function("ScalarAppendGrowthToken", unused_growth_token,
        growth_context.get(), destroy_growth_context);
    growth_context.release();
    auto hash_context = std::make_unique<HashContext>();
    hash_context->audit = std::make_shared<HashAudit>();
    Value hash = runtime.make_native_function("ScalarAppendGrowthKey.__hash__", callback_hash,
        hash_context.get(), destroy_hash_context);
    hash_context.release();
    Value klass = Value::class_object("ScalarAppendGrowthKey", {{"__hash__", hash}});
    Value key = Value::instance(klass);
    auto* instance = value_as_instance(key);
    instance->attrs.emplace_back("__xlang3_scalar_append_audit__", token);
    instance->native_get_attr = growth_getter;
    std::string error;
    bool ok = mapping_set_item(dictionary, key, Value::int64(1), error);
    for (int64_t scalar = 10; scalar != 13 && ok; ++scalar) {
      error.clear();
      ok = mapping_set_item(dictionary, Value::int64(scalar), Value::int64(scalar), error);
    }
    ok = ok && warm(runtime, dictionary) && fresh(*storage, false) &&
        storage->entries.size() == storage->integer_index.size() / 2;
    growth_audit->storage = storage;
    growth_audit->armed = true;
    error.clear();
    ok = ok && mapping_set_item(dictionary, Value::int64(13), Value::int64(13), error) &&
        growth_audit->saw_flat_rebuild && storage->runtime_hash_index.empty() &&
        storage->runtime_hash_indexed_entry_count != storage->entries.size() &&
        storage->intrinsic_hash_checked_entry_count == storage->entries.size() &&
        !storage->intrinsic_hash_keys_only;
    growth_audit->armed = false;
    ok = ok && warm(runtime, dictionary) && fresh(*storage, false) &&
        bucket_contains(*storage, Value::int64(13), 4);
    expect_true(result, ok, "mixed integer flat growth must not certify runtime buckets changed by a native getter");
  }

  {
    Value dictionary = Value::dict_reserved(0);
    auto* storage = value_as_dict(dictionary);
    std::string error;
    bool ok = warm(runtime, dictionary) &&
        mapping_set_item(dictionary, Value::int64(10), Value::int64(1), error) &&
        mapping_set_item(dictionary, Value::int64(20), Value::int64(2), error) && fresh(*storage, true);
    const auto buckets = storage->runtime_hash_index;
    error.clear();
    ok = ok && mapping_set_item(dictionary, Value::int64(10), Value::int64(3), error) &&
        storage->entries.size() == 2 && storage->entries[0].second.as.i64 == 3 &&
        storage->runtime_hash_index == buckets && fresh(*storage, true);
    error.clear();
    ok = ok && mapping_delete_item(dictionary, Value::int64(10), error) &&
        storage->runtime_hash_indexed_entry_count != storage->entries.size() &&
        storage->intrinsic_hash_checked_entry_count != storage->entries.size();
    error.clear();
    ok = ok && mapping_set_item(dictionary, Value::int64(30), Value::int64(4), error) &&
        storage->entries.size() == 2 && storage->entries[0].first.as.i64 == 20 &&
        storage->runtime_hash_indexed_entry_count != storage->entries.size() &&
        storage->intrinsic_hash_checked_entry_count != storage->entries.size() &&
        warm(runtime, dictionary) && fresh(*storage, true);
    Value copy = mapping_copy(dictionary);
    auto* copied = value_as_dict(copy);
    ok = ok && copied->runtime_hash_indexed_entry_count != copied->entries.size() &&
        copied->intrinsic_hash_checked_entry_count != copied->entries.size();
    error.clear();
    ok = ok && mapping_clear(dictionary, error) && storage->entries.empty() &&
        storage->runtime_hash_index.empty() && fresh(*storage, true);
    error.clear();
    ok = ok && mapping_set_item(dictionary, Value::int64(40), Value::int64(5), error) &&
        fresh(*storage, true) && bucket_contains(*storage, Value::int64(40), 0);
    expect_true(result, ok, "overwrite/order, delete plus same-size append, copy, and clear/reuse must keep index contracts");
  }

  // Inputs are references into entries and intentionally borrowed Values.
  // Reallocation must not leave stored keys/items borrowing their old owners.
  {
    Value dictionary = Value::dict_reserved(1);
    std::string error;
    bool ok = mapping_set_item(dictionary, Value::string("source payload"), Value::string("destination payload"), error) &&
        warm(runtime, dictionary);
    auto* storage = value_as_dict(dictionary);
    error.clear();
    ok = ok && mapping_set_item(dictionary, storage->entries[0].second, storage->entries[0].first, error) &&
        storage->entries.size() == 2 && fresh(*storage, true) &&
        value_key_equal(storage->entries[1].first, Value::string("destination payload")) &&
        value_is(storage->entries[1].second, storage->entries[0].first);
    Value borrowed;
    borrowed.tag = ValueTag::Object;
    borrowed.flags = kXlangValueBorrowedRefFlag;
    borrowed.as.obj = storage->entries[0].second.as.obj;
    // Identifier strings can be immortal/interned and intentionally skip
    // refcount changes. This spaced payload must exercise a mortal owner.
    const auto* borrowed_string = value_as_string(borrowed);
    const bool mortal = borrowed_string != nullptr && !string_object_is_immortal(*borrowed_string);
    expect_true(result, mortal, "borrowed scalar append refcount proof requires a mortal string payload");
    const auto refs = borrowed.as.obj->refcnt.load();
    error.clear();
    ok = ok && mortal && mapping_set_item(dictionary, Value::string("borrowed"), borrowed, error) &&
        fresh(*storage, true) && storage->entries[2].second.flags == 0 &&
        borrowed.as.obj->refcnt.load() == refs + 1;
    expect_true(result, ok, "scalar append must own borrowed/entry-aliased inputs before storage reallocates");
  }

  {
    Value dictionary = Value::dict_reserved(0);
    std::string error;
    bool ok = warm(runtime, dictionary) &&
        mapping_set_item(dictionary, Value::int64(77), Value::int64(8), error);
    value_set_invalid(dictionary);
    dictionary = Value::dict_reserved(0);
    auto* storage = value_as_dict(dictionary);
    ok = ok && storage->entries.empty() && storage->runtime_hash_index.empty() &&
        storage->runtime_hash_indexed_entry_count != 0 &&
        storage->intrinsic_hash_checked_entry_count != 0 && !storage->intrinsic_hash_keys_only;
    error.clear();
    ok = ok && mapping_set_item(dictionary, Value::int64(88), Value::int64(9), error) &&
        storage->runtime_hash_indexed_entry_count != storage->entries.size() &&
        storage->intrinsic_hash_checked_entry_count != storage->entries.size();
    expect_true(result, ok, "recycled dict storage must not inherit a prior scalar table's certificates");
  }
}

} // namespace xlang3::test
