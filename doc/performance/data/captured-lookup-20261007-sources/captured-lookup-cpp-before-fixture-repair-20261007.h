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
#include "xlang3/module_object.h"
#include "xlang3/sequence.h"
#include "xlang3/value_hash.h"
#include "xlang3/builtin_methods.h"

#include <array>
#include <functional>
#include <thread>

namespace xlang3::test {

inline void check_mapping_iterator_ownership(CaseResult& result,
                                             bool test_self_replacing_iterator = true) {
  {
    std::string error;
    Value source = Value::dict({});
    const Value key = Value::tuple({Value::bytes("capture"), Value::int64(7)});
    Value found = Value::int64(-1);
    expect_true(result, mapping_set_item(source, key, Value::int64(41), error) &&
        mapping_get_intrinsic_item_if_present(source, key, found) &&
        found.tag == ValueTag::Int64 && found.as.i64 == 41,
        "captured lookup shortcut must recognize a proven intrinsic index hit");
    expect_true(result, !mapping_get_intrinsic_item_if_present(
        source, Value::bytes("absent"), found) && found.as.i64 == 41,
        "speculative misses must preserve output and leave miss handling to Python");
    auto* dict = value_as_dict(source);
    dict->runtime_hash_indexed_entry_count = static_cast<size_t>(-1);
    expect_true(result, !mapping_get_intrinsic_item_if_present(source, key, found) &&
        found.as.i64 == 41,
        "speculative lookup must not silently rebuild an invalidated index");
    dict->runtime_hash_indexed_entry_count = dict->entries.size();
    expect_true(result, !mapping_get_intrinsic_item_if_present(
        source, Value::class_object("UserHashQuery", {}), found),
        "speculative lookup must reject unproven query hash protocols");
    NativeFunctionObject fake;
    fake.name = "dict.__getitem__";
    expect_true(result, !dict_is_canonical_getitem(fake),
        "a native function name alone must not authorize dict-storage dispatch");
    const auto* method = dict_find_method_spec(source, "__getitem__");
    NativeFunctionObject canonical;
    canonical.callback = method->callback;
    canonical.fast_callback = method->fast_callback;
    expect_true(result, dict_is_canonical_getitem(canonical),
        "the registered dict callback and adapter must authorize canonical dispatch");
    canonical.user_data = &source;
    expect_true(result, !dict_is_canonical_getitem(canonical),
        "contextual native callbacks must not bypass their user data");
  }
  {
    std::string error;
    Value source = Value::dict({});
    const Value key = Value::int64(7);
    Value payload = Value::list({Value::int64(29)});
    expect_true(result, mapping_set_item(source, key, payload, error),
        "finalizer probe must initialize its indexed result");
    value_set_invalid(payload);
    struct CleanupState { Value* source; bool called = false; } state{&source};
    Value out = Value::native_function(0, "lookup_output_cleanup", nullptr, &state,
        [](void* opaque) {
          auto& value = *static_cast<CleanupState*>(opaque);
          value.called = true;
          std::string cleanup_error;
          mapping_clear(*value.source, cleanup_error);
        }, nullptr, false, nullptr, true);
    expect_true(result, mapping_get_intrinsic_item_if_present(source, key, out) &&
        state.called && value_as_dict(source)->entries.empty() &&
        value_as_list(out) != nullptr && value_as_list(out)->items[0].as.i64 == 29,
        "lookup must own its result before old-output cleanup clears the dictionary");
    Value alias = Value::dict({});
    expect_true(result, mapping_set_item(alias, key, out, error) &&
        mapping_get_intrinsic_item_if_present(alias, key, alias) &&
        value_as_list(alias) != nullptr && value_as_list(alias)->items[0].as.i64 == 29,
        "lookup result must survive when output replaces the sole dictionary owner");
  }
  {
    const size_t uncached = static_cast<size_t>(-1);
    std::string error;
    Value payload = Value::bytes("immutable hash payload");
    size_t payload_hash = 0;
    expect_true(result, value_hash_key(payload, payload_hash, error) &&
        value_as_bytes(payload)->cached_hash.load(std::memory_order_relaxed) == payload_hash,
        "immutable bytes must cache the original hash without changing its value");
    Value nested = Value::tuple({payload, Value::int64(1)});
    Value key = Value::tuple({nested, Value::none()});
    size_t key_hash = 0;
    expect_true(result, value_hash_key(key, key_hash, error) &&
        value_as_tuple(key)->cached_intrinsic_hash.load(std::memory_order_relaxed) == key_hash &&
        value_as_tuple(nested)->cached_intrinsic_hash.load(std::memory_order_relaxed) != uncached,
        "nested completed immutable tuples must publish recursively proven caches");
    const auto* old_block = nested.as.obj;
    value_set_invalid(key);
    value_set_invalid(nested);
    Value reused = Value::tuple({payload, Value::int64(2)});
    expect_true(result, reused.as.obj == old_block &&
        value_as_tuple(reused)->cached_intrinsic_hash.load(std::memory_order_relaxed) == uncached,
        "tuple freelist reuse must discard the prior object's cached hash");

    Value builder = Value::tuple_reserved(2);
    auto* tuple = value_as_tuple(builder);
    tuple->items.push_back(Value::int64(7));
    size_t partial_hash = 0;
    expect_true(result, value_hash_key(builder, partial_hash, error) &&
        tuple->cached_intrinsic_hash.load(std::memory_order_relaxed) == uncached,
        "a partially populated reserved tuple must remain uncached");
    tuple->items.push_back(Value::int64(9));
    tuple_object_complete_construction(*tuple);
    Value reference = Value::tuple({Value::int64(7), Value::int64(9)});
    size_t complete_hash = 0, reference_hash = 0;
    expect_true(result, value_hash_key(builder, complete_hash, error) &&
        value_hash_key(reference, reference_hash, error) && complete_hash == reference_hash,
        "completing a reserved tuple must hash its final contents");
    tuple_object_begin_construction(*tuple);
    value_set_invalid(tuple->items[1]);
    expect_true(result, !value_hash_key(builder, complete_hash, error) &&
        tuple->cached_intrinsic_hash.load(std::memory_order_relaxed) == uncached,
        "invalidating tuple edges must discard hashes and preserve failed hashing");
    error.clear();
    Value unknown = Value::tuple({Value::class_object("CustomHashDomain", {})});
    expect_true(result, value_hash_key(unknown, complete_hash, error) &&
        value_as_tuple(unknown)->cached_intrinsic_hash.load(std::memory_order_relaxed) == uncached,
        "native identity fallback must not populate a runtime Python hash cache");

    // Shared immutable readers can race to calculate the same first hash.
    Value shared = Value::tuple({Value::bytes("shared"), Value::int64(19)});
    std::array<bool, 4> valid = {true, true, true, true};
    std::array<size_t, 4> hashes{};
    std::array<std::thread, 4> readers;
    for (size_t i = 0; i < readers.size(); ++i) {
      readers[i] = std::thread([&, i] {
        std::string thread_error;
        for (size_t j = 0; j < 128; ++j) {
          size_t hash = 0;
          if (!value_hash_key(shared, hash, thread_error)) valid[i] = false;
          if (j != 0 && hashes[i] != hash) valid[i] = false;
          hashes[i] = hash;
        }
      });
    }
    for (auto& reader : readers) reader.join();
    expect_true(result, valid[0] && valid[1] && valid[2] && valid[3] &&
        hashes[0] == hashes[1] && hashes[1] == hashes[2] && hashes[2] == hashes[3],
        "simultaneous immutable hash readers must agree without data races");
  }
  {
    std::string error;
    Value source = Value::dict({});
    Value key = Value::tuple({Value::bytes("indexed"), Value::int64(1)});
    const bool inserted = mapping_set_item(source, key, Value::int64(7), error);
    auto* dict = value_as_dict(source);
    expect_true(result, inserted && dict != nullptr &&
        dict->intrinsic_hash_keys_only &&
        dict->runtime_hash_indexed_entry_count == 1 &&
        dict->intrinsic_hash_checked_entry_count == 1,
        "intrinsic tuple writes must populate and retain a valid hash index");
    expect_true(result, key.as.obj->refcnt.load(std::memory_order_relaxed) == 2,
        "hash buckets must store scalar locations, not additional key owners");
    Value equal = Value::tuple({Value::bytes("indexed"), Value::boolean(true)});
    const bool updated = mapping_set_item(source, equal, Value::int64(9), error);
    expect_true(result, updated && dict->entries.size() == 1 &&
        value_is(dict->entries.front().first, key),
        "equal distinct intrinsic keys must update the original ordered entry");

    // Floating and protocol keys are deliberately outside the intrinsic guard.
    // A rejected key set is cached for repeated writes, then reconsidered after
    // deletion changes entry locations; it must not become a permanent fallback.
    mapping_set_item(source, Value::number(0.5), Value::int64(10), error);
    mapping_set_item(source, equal, Value::int64(11), error);
    expect_true(result, !dict->intrinsic_hash_keys_only &&
        dict->intrinsic_hash_checked_entry_count == dict->entries.size(),
        "mixed key sets must reject and cache intrinsic-index eligibility");
    mapping_delete_item(source, Value::number(0.5), error);
    expect_true(result, dict->intrinsic_hash_checked_entry_count == static_cast<size_t>(-1),
        "entry compaction must invalidate intrinsic-index eligibility");
    mapping_set_item(source, equal, Value::int64(12), error);
    expect_true(result, dict->intrinsic_hash_keys_only &&
        dict->runtime_hash_indexed_entry_count == dict->entries.size(),
        "removing a mixed key must permit the guarded index again");
    const size_t capacity = dict->entries.capacity();
    mapping_clear(source, error);
    expect_true(result, dict->entries.empty() && dict->entries.capacity() == capacity &&
        dict->runtime_hash_indexed_entry_count == 0 &&
        dict->intrinsic_hash_checked_entry_count == 0,
        "ordinary clear must reset indices while retaining entry capacity");
    mapping_set_item(source, key, Value::int64(13), error);
    Value popped;
    mapping_popitem(source, popped, error);
    expect_true(result, dict->intrinsic_hash_checked_entry_count == static_cast<size_t>(-1),
        "popitem must invalidate cached scalar entry locations");
  }
  const auto make_source = [] {
    return Value::dict({{
        Value::tuple({Value::int64(3), Value::int64(5)}),
        Value::list({Value::int64(7), Value::int64(9)})}});
  };
  const auto make_iterator = [](const Value& source, DictIterationKind kind,
                                Value& iterator, std::string& error) {
    Value view = kind == DictIterationKind::Keys ? mapping_keys_view(source)
        : kind == DictIterationKind::Values ? mapping_values_view(source)
        : mapping_items_view(source);
    return mapping_get_iter(view, iterator, error);
  };
  const auto valid_yield = [](const Value& yielded, DictIterationKind kind) {
    const Value* key = kind == DictIterationKind::Keys ? &yielded : nullptr;
    const Value* value = kind == DictIterationKind::Values ? &yielded : nullptr;
    if (kind == DictIterationKind::Items) {
      const auto* pair = value_as_tuple(yielded);
      if (pair == nullptr || pair->items.size() != 2) return false;
      key = &pair->items[0];
      value = &pair->items[1];
    }
    if (key != nullptr) {
      const auto* tuple = value_as_tuple(*key);
      if (tuple == nullptr || tuple->items.size() != 2 ||
          tuple->items[0].tag != ValueTag::Int64 || tuple->items[0].as.i64 != 3 ||
          tuple->items[1].tag != ValueTag::Int64 || tuple->items[1].as.i64 != 5) return false;
    }
    if (value != nullptr) {
      const auto* list = value_as_list(*value);
      if (list == nullptr || list->items.size() != 2 ||
          list->items[0].tag != ValueTag::Int64 || list->items[0].as.i64 != 7 ||
          list->items[1].tag != ValueTag::Int64 || list->items[1].as.i64 != 9) return false;
    }
    return true;
  };

  for (const auto kind : {DictIterationKind::Keys, DictIterationKind::Values,
                          DictIterationKind::Items}) {
    std::string error;
    Value source = make_source();
    Value iterator;
    bool done = false;
    const bool initialized = make_iterator(source, kind, iterator, error);
    expect_true(result, initialized, "dictionary view iterator must initialize");
    if (!initialized) continue;
    // Replacing the external source owner must leave the yielded object owned
    // independently of the iterator and its dictionary storage.
    const bool advanced = mapping_iter_next(iterator, done, source, error);
    expect_true(result, advanced && !done, "dictionary yield may replace the external source owner");
    value_set_invalid(iterator);
    expect_true(result, valid_yield(source, kind), "dictionary yield must survive iterator destruction");

    // The full runtime suite always checks self-replacement. A standalone
    // control probe may skip it: the old implementation accesses it after
    // replacing its sole owner, so running that path invokes undefined behavior.
    if (test_self_replacing_iterator) {
      source = make_source();
      if (!make_iterator(source, kind, iterator, error)) {
        expect_true(result, false, "aliased dictionary iterator must initialize");
        continue;
      }
      value_set_invalid(source);
      const bool aliased = mapping_iter_next(iterator, done, iterator, error);
      expect_true(result, aliased && !done && valid_yield(iterator, kind),
          "dictionary yield may replace the sole iterator owner");

      source = Value::dict({});
      if (!make_iterator(source, kind, iterator, error)) {
        expect_true(result, false, "empty dictionary iterator must initialize");
        continue;
      }
      value_set_invalid(source);
      expect_true(result, mapping_iter_next(iterator, done, iterator, error) && done &&
          iterator.tag == ValueTag::None, "exhaustion may replace the sole iterator owner");
    }

    if (kind == DictIterationKind::Items) continue;
    source = make_source();
    if (!make_iterator(source, kind, iterator, error)) {
      expect_true(result, false, "borrowed-output dictionary iterator must initialize");
      continue;
    }
    const auto* dict = value_as_dict(source);
    Value yielded;
    value_borrow_assign_fast(yielded, kind == DictIterationKind::Keys
        ? dict->entries[0].first : dict->entries[0].second);
    const bool borrowed_advanced = mapping_iter_next(iterator, done, yielded, error);
    const bool owns_result = (yielded.flags & kXlangValueBorrowedRefFlag) == 0;
    expect_true(result, borrowed_advanced && !done && owns_result,
        "dictionary yield must acquire ownership when output already borrows that object");
    if (owns_result) {
      value_set_invalid(source);
      value_set_invalid(iterator);
      expect_true(result, valid_yield(yielded, kind),
          "formerly borrowed dictionary yield must survive all source owners");
    }
  }

  // Module/class proxies synthesize entries rather than owning a dict vector.
  // Verify that moving their selected result preserves its independent owner.
  for (const bool module_source : {false, true}) {
    for (const auto kind : {DictIterationKind::Keys, DictIterationKind::Values,
                            DictIterationKind::Items}) {
      std::string error;
      Value source = module_source ? Value::module("iterator_owner_probe")
          : Value::class_object("IteratorOwnerProbe", {
              {"payload", Value::list({Value::int64(7), Value::int64(9)})}});
      if (module_source) {
        expect_true(result, module_set_attr(source, "payload",
            Value::list({Value::int64(7), Value::int64(9)}), error),
            "module iterator payload must initialize");
      }
      Value proxy = mapping_proxy(source);
      Value iterator;
      if (!make_iterator(proxy, kind, iterator, error)) {
        expect_true(result, false, "namespace proxy iterator must initialize");
        continue;
      }
      value_set_invalid(proxy);
      value_set_invalid(source);
      Value yielded;
      bool done = false;
      const auto selected_payload = [&](const Value& item) {
        const Value* key = kind == DictIterationKind::Keys ? &item : nullptr;
        const Value* value = kind == DictIterationKind::Values ? &item : nullptr;
        if (kind == DictIterationKind::Items) {
          const auto* tuple = value_as_tuple(item);
          if (tuple == nullptr || tuple->items.size() != 2) return false;
          key = &tuple->items[0];
          value = &tuple->items[1];
        }
        if (key != nullptr) {
          const auto* text = value_as_string(*key);
          if (text == nullptr || string_object_view(*text) != "payload") return false;
        }
        return value == nullptr || valid_yield(*value, DictIterationKind::Values);
      };
      bool found = false;
      while (mapping_iter_next(iterator, done, yielded, error) && !done) {
        if (selected_payload(yielded)) {
          found = true;
          break;
        }
      }
      expect_true(result, found, "namespace proxy must yield its payload entry");
      value_set_invalid(iterator);
      expect_true(result, found && selected_payload(yielded),
          "namespace proxy result must survive the source and iterator");
    }
  }
}

}  // namespace xlang3::test
