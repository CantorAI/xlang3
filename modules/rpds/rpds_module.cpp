/* Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
   Licensed under the Apache License, Version 2.0. */
#include "xlang3/xlang3.h"
#include "rpds_bridge.h"

#include <memory>

namespace {

constexpr const char* kMapType = "rpds.rpds.HashTrieMap";
constexpr const char* kSetType = "rpds.rpds.HashTrieSet";
constexpr const char* kListType = "rpds.rpds.List";
constexpr const char* kIteratorType = "rpds.rpds._MapIterator";
thread_local bool compare_failed = false;

struct PackageState {
  X3PackageHost* host = nullptr;
  X3Value map_class = x3_value_invalid();
  X3Value set_class = x3_value_invalid();
  X3Value list_class = x3_value_invalid();
  X3Value iterator_class = x3_value_invalid();
};

struct ValueBox {
  X3PackageHost* host;
  X3Value value;
  ValueBox(X3PackageHost* h, X3Value v) : host(h), value(v) {
    host->value_retain(value);
  }
  ~ValueBox() { host->value_release(value); }
};

struct MapState {
  X3RpdsMap* map;
  explicit MapState(X3RpdsMap* initial) : map(initial) {}
  ~MapState() { x3_rpds_map_free(map); }
};

struct SetState {
  X3RpdsSet* set;
  explicit SetState(X3RpdsSet* initial) : set(initial) {}
  ~SetState() { x3_rpds_set_free(set); }
};

struct ListState {
  X3RpdsList* list;
  explicit ListState(X3RpdsList* initial) : list(initial) {}
  ~ListState() { x3_rpds_list_free(list); }
};

struct IteratorState {
  enum class Kind { Map, Set, List } kind;
  void* snapshot;
  size_t index = 0;
  IteratorState(Kind k, void* s) : kind(k), snapshot(s) {}
  ~IteratorState() {
    switch (kind) {
      case Kind::Map:
        x3_rpds_map_snapshot_free(static_cast<X3RpdsMapSnapshot*>(snapshot));
        break;
      case Kind::Set:
        x3_rpds_set_snapshot_free(static_cast<X3RpdsSetSnapshot*>(snapshot));
        break;
      case Kind::List:
        x3_rpds_list_snapshot_free(static_cast<X3RpdsListSnapshot*>(snapshot));
        break;
    }
  }
};

void release_box(void* pointer) { delete static_cast<ValueBox*>(pointer); }

int32_t equal_box(void* left, void* right) {
  auto* a = static_cast<ValueBox*>(left);
  auto* b = static_cast<ValueBox*>(right);
  int32_t equal = 0;
  if (a->host->value_compare_op(a->host->runtime, X3_VALUE_COMPARE_EQ,
                                a->value, b->value, &equal) != X3_STATUS_OK) {
    compare_failed = true;
    return 0;
  }
  return equal;
}

void cleanup_map(void* data) { delete static_cast<MapState*>(data); }
void cleanup_set(void* data) { delete static_cast<SetState*>(data); }
void cleanup_list(void* data) { delete static_cast<ListState*>(data); }
void cleanup_iterator(void* data) { delete static_cast<IteratorState*>(data); }

void cleanup_package(void* data) {
  auto* state = static_cast<PackageState*>(data);
  state->host->value_release(state->map_class);
  state->host->value_release(state->set_class);
  state->host->value_release(state->list_class);
  state->host->value_release(state->iterator_class);
  delete state;
}

X3Status type_error(PackageState* state, X3CallContext* context,
                    const char* text) {
  return state->host->raise_class_error(context, "TypeError", text);
}

MapState* map_state(PackageState* state, X3CallContext* context, X3Value self) {
  auto* map = static_cast<MapState*>(
      state->host->instance_get_native_data(self, kMapType));
  if (map == nullptr) type_error(state, context, "expected HashTrieMap");
  return map;
}

SetState* set_state(PackageState* state, X3CallContext* context, X3Value self) {
  auto* set = static_cast<SetState*>(
      state->host->instance_get_native_data(self, kSetType));
  if (set == nullptr) type_error(state, context, "expected HashTrieSet");
  return set;
}

ListState* list_state(PackageState* state, X3CallContext* context, X3Value self) {
  auto* list = static_cast<ListState*>(
      state->host->instance_get_native_data(self, kListType));
  if (list == nullptr) type_error(state, context, "expected rpds List");
  return list;
}

X3Status hash_key(PackageState* state, X3Runtime* runtime, X3Value key,
                  int64_t* output) {
  X3Value hash = x3_value_invalid();
  X3Value hashed = x3_value_invalid();
  X3Status status = state->host->builtin_value(state->host, "hash", &hash);
  if (status == X3_STATUS_OK)
    status = state->host->call(runtime, hash, &key, 1, &hashed);
  if (status == X3_STATUS_OK) {
    if (hashed.tag == X3_TAG_INT64)
      *output = hashed.as.i64;
    else if (hashed.tag == X3_TAG_UINT64)
      *output = static_cast<int64_t>(hashed.as.u64);
    else
      status = X3_STATUS_ERROR;
  }
  state->host->value_release(hashed);
  state->host->value_release(hash);
  return status;
}

X3Status make_map(PackageState* state, X3Runtime* runtime, X3RpdsMap* map,
                  X3Value* result) {
  auto native = std::make_unique<MapState>(map);
  X3Value instance = state->host->value_instance(runtime, state->map_class);
  if (instance.tag == X3_TAG_INVALID) return X3_STATUS_ERROR;
  if (state->host->instance_set_native_data(instance, kMapType, native.get(),
                                            cleanup_map) != X3_STATUS_OK) {
    state->host->value_release(instance);
    return X3_STATUS_ERROR;
  }
  native.release();
  *result = instance;
  return X3_STATUS_OK;
}

X3Status make_set(PackageState* state, X3Runtime* runtime, X3RpdsSet* set,
                  X3Value* result) {
  auto native = std::make_unique<SetState>(set);
  X3Value instance = state->host->value_instance(runtime, state->set_class);
  if (instance.tag == X3_TAG_INVALID) return X3_STATUS_ERROR;
  if (state->host->instance_set_native_data(instance, kSetType, native.get(),
                                            cleanup_set) != X3_STATUS_OK) {
    state->host->value_release(instance);
    return X3_STATUS_ERROR;
  }
  native.release();
  *result = instance;
  return X3_STATUS_OK;
}

X3Status make_list(PackageState* state, X3Runtime* runtime, X3RpdsList* list,
                   X3Value* result) {
  auto native = std::make_unique<ListState>(list);
  X3Value instance = state->host->value_instance(runtime, state->list_class);
  if (instance.tag == X3_TAG_INVALID) return X3_STATUS_ERROR;
  if (state->host->instance_set_native_data(instance, kListType, native.get(),
                                            cleanup_list) != X3_STATUS_OK) {
    state->host->value_release(instance);
    return X3_STATUS_ERROR;
  }
  native.release();
  *result = instance;
  return X3_STATUS_OK;
}

X3Status map_lookup(PackageState* state, X3Runtime* runtime,
                    X3RpdsMap* map, X3Value key, ValueBox** found) {
  int64_t hash = 0;
  if (hash_key(state, runtime, key, &hash) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  compare_failed = false;
  *found = static_cast<ValueBox*>(x3_rpds_map_get(
      map, new ValueBox(state->host, key), hash, release_box, equal_box));
  return compare_failed ? X3_STATUS_ERROR : X3_STATUS_OK;
}

X3Status raise_key_error(PackageState* state, X3CallContext* context,
                         X3Runtime* runtime, X3Value key) {
  X3Value klass = x3_value_invalid();
  X3Value exception = x3_value_invalid();
  X3Status status = state->host->builtin_value(state->host, "KeyError", &klass);
  if (status == X3_STATUS_OK)
    status = state->host->call(runtime, klass, &key, 1, &exception);
  if (status == X3_STATUS_OK)
    status = state->host->raise_exception(context, exception);
  state->host->value_release(exception);
  state->host->value_release(klass);
  return status == X3_STATUS_OK ? X3_STATUS_ERROR : status;
}

X3Status map_insert_value(PackageState* state, X3Runtime* runtime,
                          X3RpdsMap** map, X3Value key, X3Value value) {
  int64_t hash = 0;
  if (hash_key(state, runtime, key, &hash) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  compare_failed = false;
  X3RpdsMap* next = x3_rpds_map_insert(
      *map, new ValueBox(state->host, key), hash,
      new ValueBox(state->host, value), release_box, equal_box);
  if (compare_failed) {
    x3_rpds_map_free(next);
    return X3_STATUS_ERROR;
  }
  x3_rpds_map_free(*map);
  *map = next;
  return X3_STATUS_OK;
}

X3Status mapping_to_map(PackageState* state, X3Runtime* runtime,
                        X3Value source, X3RpdsMap** output) {
  X3Value dict_type = x3_value_invalid();
  X3Value converted = x3_value_invalid();
  X3Status status = state->host->builtin_value(state->host, "dict", &dict_type);
  if (status == X3_STATUS_OK)
    status = state->host->call(runtime, dict_type, &source, 1, &converted);
  state->host->value_release(dict_type);
  if (status != X3_STATUS_OK) return status;
  uint64_t size = 0;
  status = state->host->len(runtime, converted, &size);
  for (uint64_t index = 0; status == X3_STATUS_OK && index < size; ++index) {
    X3Value key = x3_value_invalid();
    X3Value value = x3_value_invalid();
    status = state->host->dict_get_entry(runtime, converted, index, &key,
                                         &value);
    if (status == X3_STATUS_OK)
      status = map_insert_value(state, runtime, output, key, value);
    state->host->value_release(key);
    state->host->value_release(value);
  }
  state->host->value_release(converted);
  return status;
}

X3Status map_init(X3CallContext* context, X3Runtime* runtime, void* user_data,
                  const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc < 1 || argc > 2)
    return type_error(state, context, "HashTrieMap() takes at most one argument");
  X3RpdsMap* map = x3_rpds_map_new();
  if (argc == 2) {
    auto* source = static_cast<MapState*>(
        state->host->instance_get_native_data(args[1], kMapType));
    X3Status status = X3_STATUS_OK;
    if (source != nullptr) {
      x3_rpds_map_free(map);
      map = x3_rpds_map_clone(source->map);
    } else {
      status = mapping_to_map(state, runtime, args[1], &map);
    }
    if (status != X3_STATUS_OK) {
      x3_rpds_map_free(map);
      return status;
    }
  }
  auto native = std::make_unique<MapState>(map);
  if (state->host->instance_set_native_data(args[0], kMapType, native.get(),
                                            cleanup_map) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  native.release();
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status map_len(X3CallContext* context, X3Runtime*, void* user_data,
                 const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc != 1) return type_error(state, context, "invalid HashTrieMap length");
  auto* map = map_state(state, context, args[0]);
  if (map == nullptr) return X3_STATUS_ERROR;
  *result = x3_value_uint64(x3_rpds_map_len(map->map));
  return X3_STATUS_OK;
}

X3Status map_getitem(X3CallContext* context, X3Runtime* runtime, void* user_data,
                     const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc != 2) return type_error(state, context, "HashTrieMap key required");
  auto* map = map_state(state, context, args[0]);
  if (map == nullptr) return X3_STATUS_ERROR;
  ValueBox* found = nullptr;
  if (map_lookup(state, runtime, map->map, args[1], &found) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  if (found == nullptr)
    return raise_key_error(state, context, runtime, args[1]);
  *result = found->value;
  state->host->value_retain(*result);
  return X3_STATUS_OK;
}

X3Status map_get(X3CallContext* context, X3Runtime* runtime, void* user_data,
                 const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc < 2 || argc > 3)
    return type_error(state, context, "HashTrieMap.get() requires a key");
  auto* map = map_state(state, context, args[0]);
  if (map == nullptr) return X3_STATUS_ERROR;
  ValueBox* found = nullptr;
  if (map_lookup(state, runtime, map->map, args[1], &found) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  *result = found ? found->value : (argc == 3 ? args[2] : x3_value_none());
  state->host->value_retain(*result);
  return X3_STATUS_OK;
}

X3Status map_contains(X3CallContext* context, X3Runtime* runtime, void* user_data,
                      const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc != 2) return type_error(state, context, "HashTrieMap key required");
  auto* map = map_state(state, context, args[0]);
  if (map == nullptr) return X3_STATUS_ERROR;
  ValueBox* found = nullptr;
  if (map_lookup(state, runtime, map->map, args[1], &found) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  *result = x3_value_bool(found != nullptr);
  return X3_STATUS_OK;
}

X3Status map_insert(X3CallContext* context, X3Runtime* runtime, void* user_data,
                    const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc != 3)
    return type_error(state, context, "HashTrieMap.insert() requires key and value");
  auto* map = map_state(state, context, args[0]);
  if (map == nullptr) return X3_STATUS_ERROR;
  X3RpdsMap* next = x3_rpds_map_clone(map->map);
  if (map_insert_value(state, runtime, &next, args[1], args[2]) != X3_STATUS_OK) {
    x3_rpds_map_free(next);
    return X3_STATUS_ERROR;
  }
  return make_map(state, runtime, next, result);
}

X3Status map_remove_common(X3CallContext* context, X3Runtime* runtime,
                           PackageState* state, const X3Value* args,
                           uint32_t argc, X3Value* result, bool missing_ok) {
  if (argc != 2) return type_error(state, context, "HashTrieMap key required");
  auto* map = map_state(state, context, args[0]);
  if (map == nullptr) return X3_STATUS_ERROR;
  ValueBox* found = nullptr;
  if (map_lookup(state, runtime, map->map, args[1], &found) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  if (found == nullptr) {
    if (!missing_ok) return raise_key_error(state, context, runtime, args[1]);
    return make_map(state, runtime, x3_rpds_map_clone(map->map), result);
  }
  int64_t hash = 0;
  if (hash_key(state, runtime, args[1], &hash) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  compare_failed = false;
  X3RpdsMap* next = x3_rpds_map_remove(
      map->map, new ValueBox(state->host, args[1]), hash, release_box,
      equal_box);
  if (compare_failed) {
    x3_rpds_map_free(next);
    return X3_STATUS_ERROR;
  }
  return make_map(state, runtime, next, result);
}

X3Status map_remove(X3CallContext* context, X3Runtime* runtime, void* user_data,
                    const X3Value* args, uint32_t argc, X3Value* result) {
  return map_remove_common(context, runtime,
                           static_cast<PackageState*>(user_data), args, argc,
                           result, false);
}

X3Status map_discard(X3CallContext* context, X3Runtime* runtime, void* user_data,
                     const X3Value* args, uint32_t argc, X3Value* result) {
  return map_remove_common(context, runtime,
                           static_cast<PackageState*>(user_data), args, argc,
                           result, true);
}

X3Status map_update(X3CallContext* context, X3Runtime* runtime, void* user_data,
                    const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc < 1) return type_error(state, context, "invalid HashTrieMap update");
  auto* map = map_state(state, context, args[0]);
  if (map == nullptr) return X3_STATUS_ERROR;
  X3RpdsMap* next = x3_rpds_map_clone(map->map);
  for (uint32_t index = 1; index < argc; ++index) {
    if (mapping_to_map(state, runtime, args[index], &next) != X3_STATUS_OK) {
      x3_rpds_map_free(next);
      return X3_STATUS_ERROR;
    }
  }
  return make_map(state, runtime, next, result);
}

X3Status map_convert(X3CallContext* context, X3Runtime* runtime, void* user_data,
                     const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc != 2) return type_error(state, context, "HashTrieMap.convert() requires a mapping");
  auto* native = static_cast<MapState*>(
      state->host->instance_get_native_data(args[1], kMapType));
  if (native != nullptr) {
    *result = args[1];
    state->host->value_retain(*result);
    return X3_STATUS_OK;
  }
  X3RpdsMap* map = x3_rpds_map_new();
  if (mapping_to_map(state, runtime, args[1], &map) != X3_STATUS_OK) {
    x3_rpds_map_free(map);
    return X3_STATUS_ERROR;
  }
  return make_map(state, runtime, map, result);
}

X3Status map_view(X3CallContext* context, X3Runtime* runtime,
                  PackageState* state, const X3Value* args, uint32_t argc,
                  X3Value* result, const char* view_name) {
  if (argc != 1) return type_error(state, context, "invalid HashTrieMap view");
  if (map_state(state, context, args[0]) == nullptr) return X3_STATUS_ERROR;
  X3Value importer = x3_value_invalid();
  X3Value fromlist = x3_value_invalid();
  X3Value module_name = x3_value_invalid();
  X3Value module = x3_value_invalid();
  X3Value klass = x3_value_invalid();
  X3Status status = state->host->builtin_value(state->host, "__import__", &importer);
  if (status == X3_STATUS_OK) {
    fromlist = state->host->value_list(runtime);
    module_name = state->host->value_string(runtime, "collections.abc");
    X3Value item = state->host->value_string(runtime, view_name);
    status = state->host->list_append(runtime, fromlist, item);
    state->host->value_release(item);
  }
  if (status == X3_STATUS_OK) {
    const X3Value import_args[] = {module_name, x3_value_none(),
                                   x3_value_none(), fromlist};
    status = state->host->call(runtime, importer, import_args, 4, &module);
  }
  if (status == X3_STATUS_OK)
    status = state->host->get_attr(runtime, module, view_name, &klass);
  if (status == X3_STATUS_OK)
    status = state->host->call(runtime, klass, args, 1, result);
  state->host->value_release(klass);
  state->host->value_release(module);
  state->host->value_release(module_name);
  state->host->value_release(fromlist);
  state->host->value_release(importer);
  return status;
}

X3Status map_keys(X3CallContext* c, X3Runtime* r, void* p,
                  const X3Value* a, uint32_t n, X3Value* out) {
  return map_view(c, r, static_cast<PackageState*>(p), a, n, out, "KeysView");
}
X3Status map_items(X3CallContext* c, X3Runtime* r, void* p,
                   const X3Value* a, uint32_t n, X3Value* out) {
  return map_view(c, r, static_cast<PackageState*>(p), a, n, out, "ItemsView");
}
X3Status map_values(X3CallContext* c, X3Runtime* r, void* p,
                    const X3Value* a, uint32_t n, X3Value* out) {
  return map_view(c, r, static_cast<PackageState*>(p), a, n, out, "ValuesView");
}

X3Status make_iterator(PackageState* state, X3Runtime* runtime,
                       IteratorState::Kind kind, void* snapshot,
                       X3Value* result) {
  auto native = std::make_unique<IteratorState>(kind, snapshot);
  X3Value iterator = state->host->value_instance(runtime, state->iterator_class);
  if (iterator.tag == X3_TAG_INVALID) return X3_STATUS_ERROR;
  if (state->host->instance_set_native_data(iterator, kIteratorType, native.get(),
                                            cleanup_iterator) != X3_STATUS_OK) {
    state->host->value_release(iterator);
    return X3_STATUS_ERROR;
  }
  native.release();
  *result = iterator;
  return X3_STATUS_OK;
}

X3Status map_iter(X3CallContext* context, X3Runtime* runtime, void* user_data,
                  const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc != 1) return type_error(state, context, "invalid HashTrieMap iterator");
  auto* map = map_state(state, context, args[0]);
  if (map == nullptr) return X3_STATUS_ERROR;
  return make_iterator(state, runtime, IteratorState::Kind::Map,
                       x3_rpds_map_snapshot(map->map), result);
}

X3Status iterator_iter(X3CallContext* context, X3Runtime*, void* user_data,
                       const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc != 1) return type_error(state, context, "invalid rpds iterator");
  *result = args[0];
  state->host->value_retain(*result);
  return X3_STATUS_OK;
}

X3Status iterator_next(X3CallContext* context, X3Runtime*, void* user_data,
                       const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc != 1) return type_error(state, context, "invalid rpds iterator");
  auto* iterator = static_cast<IteratorState*>(
      state->host->instance_get_native_data(args[0], kIteratorType));
  if (iterator == nullptr) return type_error(state, context, "invalid rpds iterator");
  void* item = nullptr;
  switch (iterator->kind) {
    case IteratorState::Kind::Map: {
      void* ignored = nullptr;
      if (x3_rpds_map_snapshot_entry(
              static_cast<X3RpdsMapSnapshot*>(iterator->snapshot),
              iterator->index, &item, &ignored) == 0)
        item = nullptr;
      break;
    }
    case IteratorState::Kind::Set:
      item = x3_rpds_set_snapshot_item(
          static_cast<X3RpdsSetSnapshot*>(iterator->snapshot), iterator->index);
      break;
    case IteratorState::Kind::List:
      item = x3_rpds_list_snapshot_item(
          static_cast<X3RpdsListSnapshot*>(iterator->snapshot), iterator->index);
      break;
  }
  if (item == nullptr)
    return state->host->raise_class_error(context, "StopIteration", "");
  ++iterator->index;
  *result = static_cast<ValueBox*>(item)->value;
  state->host->value_retain(*result);
  return X3_STATUS_OK;
}

X3Status materialize(PackageState* state, X3Runtime* runtime, X3Value source,
                     X3Value* sequence, uint64_t* count) {
  X3Value list_type = x3_value_invalid();
  X3Status status = state->host->builtin_value(state->host, "list", &list_type);
  if (status == X3_STATUS_OK)
    status = state->host->call(runtime, list_type, &source, 1, sequence);
  state->host->value_release(list_type);
  if (status == X3_STATUS_OK)
    status = state->host->len(runtime, *sequence, count);
  if (status != X3_STATUS_OK) {
    state->host->value_release(*sequence);
    *sequence = x3_value_invalid();
  }
  return status;
}

X3Status set_insert_value(PackageState* state, X3Runtime* runtime,
                          X3RpdsSet** set, X3Value value) {
  int64_t hash = 0;
  if (hash_key(state, runtime, value, &hash) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  compare_failed = false;
  X3RpdsSet* next = x3_rpds_set_insert(
      *set, new ValueBox(state->host, value), hash, release_box, equal_box);
  if (compare_failed) {
    x3_rpds_set_free(next);
    return X3_STATUS_ERROR;
  }
  x3_rpds_set_free(*set);
  *set = next;
  return X3_STATUS_OK;
}

X3Status set_contains_value(PackageState* state, X3Runtime* runtime,
                            X3RpdsSet* set, X3Value value, bool* found) {
  int64_t hash = 0;
  if (hash_key(state, runtime, value, &hash) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  compare_failed = false;
  *found = x3_rpds_set_contains(
      set, new ValueBox(state->host, value), hash, release_box, equal_box) != 0;
  return compare_failed ? X3_STATUS_ERROR : X3_STATUS_OK;
}

X3Status iterable_to_set(PackageState* state, X3Runtime* runtime,
                         X3Value source, X3RpdsSet** set) {
  X3Value sequence = x3_value_invalid();
  uint64_t count = 0;
  if (materialize(state, runtime, source, &sequence, &count) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  X3Status status = X3_STATUS_OK;
  for (uint64_t index = 0; index < count && status == X3_STATUS_OK; ++index) {
    X3Value element = x3_value_invalid();
    status = state->host->get_item(runtime, sequence, x3_value_uint64(index),
                                   &element);
    if (status == X3_STATUS_OK)
      status = set_insert_value(state, runtime, set, element);
    state->host->value_release(element);
  }
  state->host->value_release(sequence);
  return status;
}

X3Status set_init(X3CallContext* context, X3Runtime* runtime, void* user_data,
                  const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc < 1 || argc > 2)
    return type_error(state, context, "HashTrieSet() takes at most one argument");
  X3RpdsSet* set = x3_rpds_set_new();
  if (argc == 2) {
    auto* source = static_cast<SetState*>(
        state->host->instance_get_native_data(args[1], kSetType));
    X3Status status = X3_STATUS_OK;
    if (source != nullptr) {
      x3_rpds_set_free(set);
      set = x3_rpds_set_clone(source->set);
    } else {
      status = iterable_to_set(state, runtime, args[1], &set);
    }
    if (status != X3_STATUS_OK) {
      x3_rpds_set_free(set);
      return status;
    }
  }
  auto native = std::make_unique<SetState>(set);
  if (state->host->instance_set_native_data(args[0], kSetType, native.get(),
                                            cleanup_set) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  native.release();
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status set_len(X3CallContext* context, X3Runtime*, void* user_data,
                 const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc != 1) return type_error(state, context, "invalid HashTrieSet length");
  auto* set = set_state(state, context, args[0]);
  if (set == nullptr) return X3_STATUS_ERROR;
  *result = x3_value_uint64(x3_rpds_set_len(set->set));
  return X3_STATUS_OK;
}

X3Status set_contains(X3CallContext* context, X3Runtime* runtime,
                      void* user_data, const X3Value* args, uint32_t argc,
                      X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc != 2) return type_error(state, context, "HashTrieSet value required");
  auto* set = set_state(state, context, args[0]);
  if (set == nullptr) return X3_STATUS_ERROR;
  bool found = false;
  if (set_contains_value(state, runtime, set->set, args[1], &found) !=
      X3_STATUS_OK) return X3_STATUS_ERROR;
  *result = x3_value_bool(found);
  return X3_STATUS_OK;
}

X3Status set_insert(X3CallContext* context, X3Runtime* runtime, void* user_data,
                    const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc != 2) return type_error(state, context, "HashTrieSet value required");
  auto* set = set_state(state, context, args[0]);
  if (set == nullptr) return X3_STATUS_ERROR;
  X3RpdsSet* next = x3_rpds_set_clone(set->set);
  if (set_insert_value(state, runtime, &next, args[1]) != X3_STATUS_OK) {
    x3_rpds_set_free(next);
    return X3_STATUS_ERROR;
  }
  return make_set(state, runtime, next, result);
}

X3Status set_remove_common(X3CallContext* context, X3Runtime* runtime,
                           PackageState* state, const X3Value* args,
                           uint32_t argc, X3Value* result, bool missing_ok) {
  if (argc != 2) return type_error(state, context, "HashTrieSet value required");
  auto* set = set_state(state, context, args[0]);
  if (set == nullptr) return X3_STATUS_ERROR;
  bool found = false;
  if (set_contains_value(state, runtime, set->set, args[1], &found) !=
      X3_STATUS_OK) return X3_STATUS_ERROR;
  if (!found) {
    if (!missing_ok) return raise_key_error(state, context, runtime, args[1]);
    return make_set(state, runtime, x3_rpds_set_clone(set->set), result);
  }
  int64_t hash = 0;
  if (hash_key(state, runtime, args[1], &hash) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  compare_failed = false;
  X3RpdsSet* next = x3_rpds_set_remove(
      set->set, new ValueBox(state->host, args[1]), hash, release_box,
      equal_box);
  if (compare_failed) {
    x3_rpds_set_free(next);
    return X3_STATUS_ERROR;
  }
  return make_set(state, runtime, next, result);
}

X3Status set_remove(X3CallContext* c, X3Runtime* r, void* p,
                    const X3Value* a, uint32_t n, X3Value* out) {
  return set_remove_common(c, r, static_cast<PackageState*>(p), a, n, out,
                           false);
}
X3Status set_discard(X3CallContext* c, X3Runtime* r, void* p,
                     const X3Value* a, uint32_t n, X3Value* out) {
  return set_remove_common(c, r, static_cast<PackageState*>(p), a, n, out,
                           true);
}

X3Status set_update(X3CallContext* context, X3Runtime* runtime, void* user_data,
                    const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc < 1) return type_error(state, context, "invalid HashTrieSet update");
  auto* set = set_state(state, context, args[0]);
  if (set == nullptr) return X3_STATUS_ERROR;
  X3RpdsSet* next = x3_rpds_set_clone(set->set);
  for (uint32_t index = 1; index < argc; ++index) {
    if (iterable_to_set(state, runtime, args[index], &next) != X3_STATUS_OK) {
      x3_rpds_set_free(next);
      return X3_STATUS_ERROR;
    }
  }
  return make_set(state, runtime, next, result);
}

X3Status set_iter(X3CallContext* context, X3Runtime* runtime, void* user_data,
                  const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc != 1) return type_error(state, context, "invalid HashTrieSet iterator");
  auto* set = set_state(state, context, args[0]);
  if (set == nullptr) return X3_STATUS_ERROR;
  return make_iterator(state, runtime, IteratorState::Kind::Set,
                       x3_rpds_set_snapshot(set->set), result);
}

X3Status list_init(X3CallContext* context, X3Runtime* runtime, void* user_data,
                   const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc < 1) return type_error(state, context, "invalid rpds List construction");
  X3RpdsList* list = x3_rpds_list_new();
  if (argc == 2) {
    auto* source = static_cast<ListState*>(
        state->host->instance_get_native_data(args[1], kListType));
    if (source != nullptr) {
      x3_rpds_list_free(list);
      list = x3_rpds_list_clone(source->list);
    } else {
      X3Value sequence = x3_value_invalid();
      uint64_t count = 0;
      if (materialize(state, runtime, args[1], &sequence, &count) !=
          X3_STATUS_OK) {
        x3_rpds_list_free(list);
        return X3_STATUS_ERROR;
      }
      for (uint64_t index = count; index > 0; --index) {
        X3Value element = x3_value_invalid();
        X3Status status = state->host->get_item(
            runtime, sequence, x3_value_uint64(index - 1), &element);
        if (status == X3_STATUS_OK) {
          X3RpdsList* next = x3_rpds_list_push_front(
              list, new ValueBox(state->host, element), release_box);
          x3_rpds_list_free(list);
          list = next;
        }
        state->host->value_release(element);
        if (status != X3_STATUS_OK) {
          state->host->value_release(sequence);
          x3_rpds_list_free(list);
          return status;
        }
      }
      state->host->value_release(sequence);
    }
  } else if (argc > 2) {
    for (uint32_t index = argc; index > 1; --index) {
      X3RpdsList* next = x3_rpds_list_push_front(
          list, new ValueBox(state->host, args[index - 1]), release_box);
      x3_rpds_list_free(list);
      list = next;
    }
  }
  auto native = std::make_unique<ListState>(list);
  if (state->host->instance_set_native_data(args[0], kListType, native.get(),
                                            cleanup_list) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  native.release();
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status list_len(X3CallContext* context, X3Runtime*, void* user_data,
                  const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc != 1) return type_error(state, context, "invalid rpds List length");
  auto* list = list_state(state, context, args[0]);
  if (list == nullptr) return X3_STATUS_ERROR;
  *result = x3_value_uint64(x3_rpds_list_len(list->list));
  return X3_STATUS_OK;
}

X3Status list_push_front(X3CallContext* context, X3Runtime* runtime,
                         void* user_data, const X3Value* args, uint32_t argc,
                         X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc != 2) return type_error(state, context, "List.push_front() requires a value");
  auto* list = list_state(state, context, args[0]);
  if (list == nullptr) return X3_STATUS_ERROR;
  X3RpdsList* next = x3_rpds_list_push_front(
      list->list, new ValueBox(state->host, args[1]), release_box);
  return make_list(state, runtime, next, result);
}

X3Status list_drop_first(X3CallContext* context, X3Runtime* runtime,
                         void* user_data, const X3Value* args, uint32_t argc,
                         X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc != 1) return type_error(state, context, "invalid List.drop_first()");
  auto* list = list_state(state, context, args[0]);
  if (list == nullptr) return X3_STATUS_ERROR;
  X3RpdsList* next = x3_rpds_list_drop_first(list->list);
  if (next == nullptr)
    return state->host->raise_class_error(context, "IndexError", "index out of range");
  return make_list(state, runtime, next, result);
}

X3Status list_iter(X3CallContext* context, X3Runtime* runtime, void* user_data,
                   const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc != 1) return type_error(state, context, "invalid rpds List iterator");
  auto* list = list_state(state, context, args[0]);
  if (list == nullptr) return X3_STATUS_ERROR;
  return make_iterator(state, runtime, IteratorState::Kind::List,
                       x3_rpds_list_snapshot(list->list), result);
}

void method(X3NativeFunctionDef& def, const char* name, X3NativeFn callback,
            PackageState* state) {
  def = {sizeof(X3NativeFunctionDef), name, callback, state, 0, 0, 0, nullptr};
}

X3Status register_module(X3PackageHost* host) {
  auto state = std::make_unique<PackageState>();
  state->host = host;
  X3Module* module = nullptr;
  if (host->add_module(host, "rpds.rpds", &module) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  X3NativeFunctionDef map_methods[14]{};
  method(map_methods[0], "__init__", map_init, state.get());
  method(map_methods[1], "__len__", map_len, state.get());
  method(map_methods[2], "__getitem__", map_getitem, state.get());
  method(map_methods[3], "get", map_get, state.get());
  method(map_methods[4], "__contains__", map_contains, state.get());
  method(map_methods[5], "insert", map_insert, state.get());
  method(map_methods[6], "__iter__", map_iter, state.get());
  method(map_methods[7], "remove", map_remove, state.get());
  method(map_methods[8], "discard", map_discard, state.get());
  method(map_methods[9], "update", map_update, state.get());
  method(map_methods[10], "convert", map_convert, state.get());
  method(map_methods[11], "keys", map_keys, state.get());
  method(map_methods[12], "items", map_items, state.get());
  method(map_methods[13], "values", map_values, state.get());
  if (host->module_add_class(module, "HashTrieMap", map_methods, 14,
                             &state->map_class) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  X3NativeFunctionDef set_methods[8]{};
  method(set_methods[0], "__init__", set_init, state.get());
  method(set_methods[1], "__len__", set_len, state.get());
  method(set_methods[2], "__contains__", set_contains, state.get());
  method(set_methods[3], "insert", set_insert, state.get());
  method(set_methods[4], "remove", set_remove, state.get());
  method(set_methods[5], "discard", set_discard, state.get());
  method(set_methods[6], "update", set_update, state.get());
  method(set_methods[7], "__iter__", set_iter, state.get());
  if (host->module_add_class(module, "HashTrieSet", set_methods, 8,
                             &state->set_class) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  X3NativeFunctionDef list_methods[5]{};
  method(list_methods[0], "__init__", list_init, state.get());
  method(list_methods[1], "__len__", list_len, state.get());
  method(list_methods[2], "push_front", list_push_front, state.get());
  method(list_methods[3], "drop_first", list_drop_first, state.get());
  method(list_methods[4], "__iter__", list_iter, state.get());
  if (host->module_add_class(module, "List", list_methods, 5,
                             &state->list_class) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  {
    X3Value method_value = x3_value_invalid();
    X3Value classmethod_type = x3_value_invalid();
    X3Value descriptor = x3_value_invalid();
    X3Status status = host->get_attr(host->runtime, state->map_class,
                                     "convert", &method_value);
    if (status == X3_STATUS_OK)
      status = host->builtin_value(host, "classmethod", &classmethod_type);
    if (status == X3_STATUS_OK)
      status = host->call(host->runtime, classmethod_type, &method_value, 1,
                          &descriptor);
    if (status == X3_STATUS_OK)
      status = host->class_add_value(state->map_class, "convert", descriptor);
    host->value_release(descriptor);
    host->value_release(classmethod_type);
    host->value_release(method_value);
    if (status != X3_STATUS_OK) return status;
  }
  X3NativeFunctionDef iterator_methods[2]{};
  method(iterator_methods[0], "__iter__", iterator_iter, state.get());
  method(iterator_methods[1], "__next__", iterator_next, state.get());
  if (host->module_add_class(module, "_MapIterator", iterator_methods, 2,
                             &state->iterator_class) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  if (host->package_set_cleanup(host, state.get(), cleanup_package) !=
      X3_STATUS_OK)
    return X3_STATUS_ERROR;
  state.release();
  return X3_STATUS_OK;
}

}  // namespace

extern "C" XLANG3_PACKAGE_EXPORT const uint32_t xlang3_package_abi_version = X3_ABI_VERSION;

extern "C" XLANG3_PACKAGE_EXPORT X3Status Load(void* host_pointer, X3Value) {
  auto* host = static_cast<X3PackageHost*>(host_pointer);
  if (host == nullptr || host->abi_version != X3_ABI_VERSION)
    return X3_STATUS_ERROR;
  host->package_set_metadata(host, "package", "rpds.rpds");
  host->package_set_metadata(host, "version", "2026.6.3+xlang3");
  return register_module(host);
}
