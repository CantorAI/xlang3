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
#include "xlang3/builtins.h"

#include "xlang3/functional_iterators.h"
#include "xlang3/generator.h"
#include "xlang3/mapping.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"
#include "xlang3/set_object.h"
#include "gc_plain_cycles.h"

#include <algorithm>
#include <atomic>
#include <array>
#include <mutex>

namespace xlang3 {

namespace {

std::mutex& tracked_objects_mutex() {
  static auto* mutex = new std::mutex();
  return *mutex;
}

std::vector<Object*>& tracked_objects() {
  static auto* objects = new std::vector<Object*>();
  return *objects;
}

bool gc_kind_may_be_tracked(ObjectKind kind) {
  switch (kind) {
    case ObjectKind::String:
    case ObjectKind::BigInt:
    case ObjectKind::Complex:
    case ObjectKind::Bytes:
    case ObjectKind::ByteArray:
    case ObjectKind::Range:
    case ObjectKind::Code:
    case ObjectKind::NativeFunction:
      return false;
    default:
      return true;
  }
}

} // namespace

void gc_track_object(Object* object) {
  if (object == nullptr || !gc_kind_may_be_tracked(object->kind)) return;
  // Recycled cached objects remain indexed with refcount zero; reuse can
  // reactivate them without serializing on the global tracker mutex.
  if ((object->gc_tracking_state.load(std::memory_order_relaxed) &
       kGcObjectIndexMask) != kGcObjectIndexNone) return;
  std::lock_guard lock(tracked_objects_mutex());
  auto& objects = tracked_objects();
  const uint64_t state = object->gc_tracking_state.load(std::memory_order_relaxed);
  if ((state & kGcObjectIndexMask) != kGcObjectIndexNone) return;
  objects.push_back(object);
  uint64_t expected = state;
  while (!object->gc_tracking_state.compare_exchange_weak(
      expected, (expected & kObjectWeakrefFlagsMask) |
          static_cast<uint64_t>(objects.size() - 1),
      std::memory_order_release, std::memory_order_relaxed)) {
  }
}

void gc_untrack_object(Object* object) {
  if (object == nullptr || !gc_kind_may_be_tracked(object->kind)) return;
  if ((object->gc_tracking_state.load(std::memory_order_relaxed) &
       kGcObjectIndexMask) == kGcObjectIndexNone) return;
  std::lock_guard lock(tracked_objects_mutex());
  auto& objects = tracked_objects();
  const uint64_t state = object->gc_tracking_state.load(std::memory_order_relaxed);
  const size_t index = static_cast<size_t>(state & kGcObjectIndexMask);
  if (index >= objects.size() || objects[index] != object) return;
  Object* last = objects.back();
  objects[index] = last;
  uint64_t last_state = last->gc_tracking_state.load(std::memory_order_relaxed);
  while (!last->gc_tracking_state.compare_exchange_weak(
      last_state, (last_state & kObjectWeakrefFlagsMask) |
          static_cast<uint64_t>(index),
      std::memory_order_release, std::memory_order_relaxed)) {
  }
  objects.pop_back();
  uint64_t object_state = object->gc_tracking_state.load(std::memory_order_relaxed);
  while (!object->gc_tracking_state.compare_exchange_weak(
      object_state, (object_state & kObjectWeakrefFlagsMask) |
          kGcObjectIndexNone,
      std::memory_order_release, std::memory_order_relaxed)) {
  }
}

bool gc_value_is_tracked(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr ||
      !gc_kind_may_be_tracked(value.as.obj->kind)) {
    return false;
  }
  if (value.as.obj->kind == ObjectKind::Tuple) {
    const auto* tuple = value_as_tuple(value);
    return tuple != nullptr && !tuple->items.empty();
  }
  return true;
}

std::vector<Value> gc_snapshot_tracked_objects() {
  std::vector<Value> result;
  {
    std::lock_guard lock(tracked_objects_mutex());
    result.reserve(tracked_objects().size());
    for (auto* object : tracked_objects()) {
      uint32_t references = object->refcnt.load(std::memory_order_acquire);
      while (references != 0) {
        if (object->refcnt.compare_exchange_weak(
                references, references + 1, std::memory_order_acq_rel,
                std::memory_order_acquire)) {
          Value value;
          value.tag = ValueTag::Object;
          value.as.obj = object;
          result.push_back(std::move(value));
          break;
        }
      }
    }
  }
  result.erase(
      std::remove_if(result.begin(), result.end(), [](const Value& value) {
        return !gc_value_is_tracked(value);
      }), result.end());
  return result;
}

void emit_pending_socket_resource_warnings(Runtime& runtime);
void emit_pending_file_resource_warnings(Runtime& runtime);

namespace {

std::atomic_bool& gc_collection_active() {
  static std::atomic_bool active{false};
  return active;
}

struct GcCollectionScope {
  ~GcCollectionScope() { gc_collection_active().store(false, std::memory_order_release); }
};

struct GcState {
  bool enabled = true;
  std::array<int64_t, 3> thresholds{700, 10, 10};
};

bool gc_enable(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void* data) {
  if (argc != 0) {
    error = "gc.enable() takes no arguments";
    return false;
  }
  static_cast<GcState*>(data)->enabled = true;
  value_set_none(out);
  return true;
}

bool gc_disable(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void* data) {
  if (argc != 0) {
    error = "gc.disable() takes no arguments";
    return false;
  }
  static_cast<GcState*>(data)->enabled = false;
  value_set_none(out);
  return true;
}

bool gc_isenabled(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void* data) {
  if (argc != 0) {
    error = "gc.isenabled() takes no arguments";
    return false;
  }
  value_set_bool(out, static_cast<GcState*>(data)->enabled);
  return true;
}

bool gc_collect(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc > 1 || (argc == 1 && (args[0].tag != ValueTag::Int64 || args[0].as.i64 < 0 || args[0].as.i64 > 2))) {
    error = "gc.collect() generation must be an integer between 0 and 2";
    return false;
  }
  // Finalizers/native cleanup can call gc.collect recursively or from another
  // thread. Keep one process-wide collection active through both passes and
  // snapshot retirement; reentry must not inspect a partly retired graph.
  if (gc_collection_active().exchange(true, std::memory_order_acq_rel)) {
    value_set_int64(out, 0);
    return true;
  }
  GcCollectionScope collection_scope;
  runtime.synchronize_modules_from_registry();
  runtime.release_dead_frame_registers();
  emit_pending_socket_resource_warnings(runtime);
  GcPhaseDiagnostic collection_diagnostic("collect");
  uint64_t collected = weakref_collect_cycles(runtime);
  collection_diagnostic.mark("specialized", collected);
  collected += gc_collect_plain_cycles();
  collection_diagnostic.mark("plain_total", collected);
  emit_pending_file_resource_warnings(runtime);
  weakref_dispatch_callbacks(runtime);
  value_set_int64(out, static_cast<int64_t>(collected));
  return true;
}

bool gc_get_count(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "gc.get_count() takes no arguments";
    return false;
  }
  out = Value::tuple({Value::int64(0), Value::int64(0), Value::int64(0)});
  return true;
}

bool gc_is_tracked(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "gc.is_tracked() takes exactly one argument";
    return false;
  }
  value_set_bool(out, gc_value_is_tracked(args[0]));
  return true;
}

bool gc_get_objects(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "gc.get_objects() generation selection is not yet supported";
    return false;
  }
  out = Value::list(gc_snapshot_tracked_objects());
  return true;
}

bool gc_object_references_any(Runtime& runtime, const Value& source,
                              const Value* targets,
                              uint32_t target_count) {
  bool found = false;
  const auto edge = [&](const Value& value) {
    if (found || value.tag != ValueTag::Object || value.as.obj == nullptr)
      return;
    for (uint32_t index = 0; index < target_count; ++index) {
      if (value_is(value, targets[index])) {
        found = true;
        return;
      }
    }
  };
  if (auto* value = value_as_list(source)) {
    for (const auto& item : value->items) edge(item);
  } else if (auto* value = value_as_tuple(source)) {
    for (const auto& item : value->items) edge(item);
  } else if (auto* value = value_as_set(source)) {
    for (const auto& item : value->items) edge(item);
  } else if (auto* value = value_as_dict(source)) {
    for (const auto& item : value->entries) {
      edge(item.first);
      edge(item.second);
    }
  } else if (auto* value = value_as_mapping_proxy(source)) {
    edge(value->source);
  } else if (auto* value = value_as_dict_view(source)) {
    edge(value->source);
  } else if (auto* value = value_as_slice(source)) {
    edge(value->start);
    edge(value->stop);
    edge(value->step);
  } else if (auto* value = value_as_memoryview(source)) {
    edge(value->owner);
    edge(value->exporter);
  } else if (auto* value = value_as_cell(source)) {
    edge(value->value);
  } else if (auto* value = value_as_module(source)) {
    for (const auto& item : value->slots) edge(item);
    for (const auto& item : value->extra_globals) edge(item.second);
    edge(value->namespace_dict);
  } else if (auto* value = value_as_function(source)) {
    edge(value->builtins);
    edge(value->globals_module);
    edge(value->annotations);
    edge(value->doc);
    edge(value->attrs_dict);
    for (const auto& item : value->closure) edge(item);
    for (const auto& item : value->defaults) edge(item);
    for (const auto& item : value->positional_defaults) edge(item);
    for (const auto& item : value->kwdefaults) edge(item.second);
    edge(value->kwdefaults_dict);
    edge(value->code_object);
  } else if (auto* value = value_as_native_function(source)) {
    if (value->attrs_dict != nullptr) edge(*value->attrs_dict);
  } else if (auto* value = value_as_generic_alias(source)) {
    edge(value->origin);
    edge(value->args);
    edge(value->klass);
  } else if (auto* value = value_as_type_param(source)) {
    edge(value->bound);
    edge(value->default_value);
  } else if (auto* value = value_as_class(source)) {
    edge(value->base);
    edge(value->metaclass);
    edge(value->globals_module);
    for (const auto& item : value->bases) edge(item);
    for (const auto& item : value->mro_cache) edge(item);
    for (const auto& item : value->attrs) edge(item.second);
  } else if (auto* value = value_as_instance(source)) {
    edge(value->klass);
    edge(value->mapping_storage);
    edge(value->sequence_storage);
    for (uint32_t index = 0; index < value->slot_count; ++index)
      edge(instance_slot_at(value, index));
    for (const auto& item : value->attrs) edge(item.second);
    instance_visit_native_gc_references(*value, [&](Object* target) {
      for (uint32_t index = 0; index < target_count; ++index)
        if (targets[index].tag == ValueTag::Object &&
            targets[index].as.obj == target)
          found = true;
    });
  } else if (auto* value = value_as_bound_method(source)) {
    edge(value->self);
    edge(value->function);
  } else if (auto* value = value_as_static_method(source)) {
    edge(value->function);
    edge(value->attrs_dict);
  } else if (auto* value = value_as_class_method(source)) {
    edge(value->function);
    edge(value->attrs_dict);
  } else if (auto* value = value_as_super(source)) {
    edge(value->klass);
    edge(value->self);
  } else if (auto* value = value_as_slot_descriptor(source)) {
    edge(value->owner_class);
  } else if (auto* value = value_as_property(source)) {
    edge(value->fget);
    edge(value->fset);
    edge(value->fdel);
    edge(value->doc);
    edge(value->name);
  } else if (auto* value = value_as_frame(source)) {
    edge(value->globals_module);
    edge(value->locals);
    if (!value->live)
      for (const auto& item : value->local_snapshot) edge(item);
    edge(value->back);
    edge(value->builtins);
    edge(value->trace);
    edge(value->generator_ref);
  } else if (auto* value = value_as_traceback(source)) {
    edge(value->frame);
    edge(value->next);
  } else if (auto* value = value_as_generator(source)) {
    edge(value->function);
    for (const auto& item : value->args) edge(item);
    edge(value->pending_send);
    edge(value->pending_throw);
    edge(value->return_value);
    edge(value->awaiting);
    edge(value->origin);
    generator_vm_visit_references(*value, edge);
    runtime.visit_active_generator_references(value, edge);
  } else if (auto* value = value_as_async_generator_awaitable(source)) {
    edge(value->generator);
    for (const auto& item : value->args) edge(item);
  }
  return found;
}

bool gc_get_referrers(Runtime& runtime, const Value* args, uint32_t argc,
                      Value& out, std::string&, void*) {
  std::vector<Value> referrers;
  for (auto& candidate : gc_snapshot_tracked_objects())
    if (gc_object_references_any(runtime, candidate, args, argc))
      referrers.push_back(std::move(candidate));
  out = Value::list(std::move(referrers));
  return true;
}

bool gc_get_threshold(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void* data) {
  if (argc != 0) {
    error = "gc.get_threshold() takes no arguments";
    return false;
  }
  const auto& values = static_cast<GcState*>(data)->thresholds;
  out = Value::tuple({Value::int64(values[0]), Value::int64(values[1]), Value::int64(values[2])});
  return true;
}

bool gc_set_threshold(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void* data) {
  if (argc < 1 || argc > 3) {
    error = "gc.set_threshold() expected 1 to 3 arguments";
    return false;
  }
  auto& values = static_cast<GcState*>(data)->thresholds;
  for (uint32_t i = 0; i < argc; ++i) {
    if (args[i].tag != ValueTag::Int64) {
      error = "gc.set_threshold() arguments must be integers";
      return false;
    }
    values[i] = args[i].as.i64;
  }
  value_set_none(out);
  return true;
}

} // namespace

void register_gc_module(Runtime& runtime) {
  auto* state = new GcState();
  runtime.register_native_package_cleanup(state, [](void* data) { delete static_cast<GcState*>(data); });
  NativeModuleBuilder builder(runtime, "gc");
  builder.value("enable", runtime.make_native_function("gc.enable", gc_enable, state))
      .value("disable", runtime.make_native_function("gc.disable", gc_disable, state))
      .value("isenabled", runtime.make_native_function("gc.isenabled", gc_isenabled, state))
      .function("collect", gc_collect)
      .function("is_tracked", gc_is_tracked)
      .function("get_objects", gc_get_objects)
      .function("get_referrers", gc_get_referrers)
      .function("get_count", gc_get_count)
      .value("get_threshold", runtime.make_native_function("gc.get_threshold", gc_get_threshold, state))
      .value("set_threshold", runtime.make_native_function("gc.set_threshold", gc_set_threshold, state))
      .value("garbage", Value::list({}))
      .value("callbacks", Value::list({}));
  runtime.register_module("gc", builder.finish());
}

} // namespace xlang3
