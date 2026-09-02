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
#include "xlang3/sequence.h"

#include "xlang3/functional_iterators.h"
#include "xlang3/generator.h"
#include "xlang3/mapping.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/perf_counters.h"
#include "xlang3/set_object.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace xlang3 {

namespace {

template <typename T>
T* allocate_sequence_object(ObjectKind kind) {
  auto* obj = new T();
  obj->header.kind = kind;
  obj->header.refcnt = 1;
  xlang_perf_count_object_alloc(kind);
  return obj;
}

struct ListObjectFreeList {
  ~ListObjectFreeList() {
    for (auto* object : items) {
      delete object;
    }
  }

  std::vector<ListObject*> items;
};

thread_local ListObjectFreeList list_object_free_list;

ListObject* allocate_list_object() {
  xlang_perf_count_object_alloc(ObjectKind::List);
  if (!list_object_free_list.items.empty()) {
    auto* obj = list_object_free_list.items.back();
    list_object_free_list.items.pop_back();
    obj->header.kind = ObjectKind::List;
    obj->header.refcnt = 1;
    return obj;
  }
  auto* obj = new ListObject();
  obj->header.kind = ObjectKind::List;
  obj->header.refcnt = 1;
  return obj;
}

void recycle_list_object(ListObject* object) {
  object->items.clear();
  if (list_object_free_list.items.size() < 4096) {
    list_object_free_list.items.push_back(object);
    return;
  }
  delete object;
}

std::string repr_items(const std::vector<Value>& items, const char* open, const char* close) {
  std::string text = open;
  for (size_t i = 0; i < items.size(); ++i) {
    if (i != 0) {
      text += ", ";
    }
    text += value_to_repr(items[i]);
  }
  text += close;
  return text;
}

bool normalize_index(int64_t raw_index, uint64_t size, uint64_t& out) {
  int64_t index = raw_index;
  const auto signed_size = static_cast<int64_t>(size);
  if (index < 0) {
    index += signed_size;
  }
  if (index < 0 || index >= signed_size) {
    return false;
  }
  out = static_cast<uint64_t>(index);
  return true;
}

int64_t range_length(int64_t start, int64_t stop, int64_t step) {
  if (step > 0) {
    if (start >= stop) {
      return 0;
    }
    return ((stop - start - 1) / step) + 1;
  }
  if (start <= stop) {
    return 0;
  }
  const int64_t neg_step = -step;
  return ((start - stop - 1) / neg_step) + 1;
}

bool slice_part_to_i64(const Value& value, int64_t& out, bool& is_none, std::string& error) {
  is_none = value.tag == ValueTag::None;
  if (is_none) {
    out = 0;
    return true;
  }
  if (value.tag != ValueTag::Int64) {
    error = "slice indices must be integers or None";
    return false;
  }
  out = value.as.i64;
  return true;
}

bool normalize_slice(const SliceObject& slice, int64_t length, int64_t& start, int64_t& stop, int64_t& step, std::string& error) {
  bool start_none = false;
  bool stop_none = false;
  bool step_none = false;
  if (!slice_part_to_i64(slice.step, step, step_none, error) ||
      !slice_part_to_i64(slice.start, start, start_none, error) ||
      !slice_part_to_i64(slice.stop, stop, stop_none, error)) {
    return false;
  }
  if (step_none) {
    step = 1;
  }
  if (step == 0) {
    error = "slice step cannot be zero";
    return false;
  }
  if (start_none) {
    start = step < 0 ? length - 1 : 0;
  } else {
    if (start < 0) start += length;
    if (step < 0) {
      if (start < 0) start = -1;
      if (start >= length) start = length - 1;
    } else {
      if (start < 0) start = 0;
      if (start > length) start = length;
    }
  }
  if (stop_none) {
    stop = step < 0 ? -1 : length;
  } else {
    if (stop < 0) stop += length;
    if (step < 0) {
      if (stop < 0) stop = -1;
      if (stop >= length) stop = length - 1;
    } else {
      if (stop < 0) stop = 0;
      if (stop > length) stop = length;
    }
  }
  return true;
}

struct BinaryStorageView {
  const char* data = nullptr;
  size_t size = 0;
  bool readonly = true;
};

BinaryStorageView binary_storage(const Value& value) {
  if (auto* bytes = value_as_bytes(value)) {
    const auto view = bytes_object_view(*bytes);
    return BinaryStorageView{view.data(), view.size(), true};
  }
  if (auto* bytearray = value_as_bytearray(value)) {
    return BinaryStorageView{bytearray->value.data(), bytearray->value.size(), false};
  }
  if (auto* view = value_as_memoryview(value)) {
    if (view->released) {
      return {};
    }
    const auto storage = binary_storage(view->owner);
    if (storage.data == nullptr || view->offset > storage.size) {
      return {};
    }
    return BinaryStorageView{storage.data + view->offset, view->size, view->readonly || storage.readonly};
  }
  return {};
}

bool struct_sequence_storage(const Value& value, Value& out) {
  if (value_as_instance(value) == nullptr) {
    return false;
  }
  std::string ignored;
  if (!object_get_attr(value, "_tuple", out, ignored)) {
    return false;
  }
  return value_as_tuple(out) != nullptr;
}

std::string binary_slice_text(std::string_view storage, int64_t start, int64_t stop, int64_t step) {
  std::string text;
  if (step > 0) {
    for (int64_t i = start; i < stop; i += step) {
      text.push_back(storage[static_cast<size_t>(i)]);
    }
  } else {
    for (int64_t i = start; i > stop; i += step) {
      text.push_back(storage[static_cast<size_t>(i)]);
    }
  }
  return text;
}

std::string utf8_slice_text(std::string_view storage, int64_t start, int64_t stop, int64_t step) {
  std::string text;
  if (step > 0) {
    for (int64_t i = start; i < stop; i += step) {
      const auto ch = utf8_codepoint_at(storage, static_cast<size_t>(i));
      text.append(ch.data(), ch.size());
    }
  } else {
    for (int64_t i = start; i > stop; i += step) {
      const auto ch = utf8_codepoint_at(storage, static_cast<size_t>(i));
      text.append(ch.data(), ch.size());
    }
  }
  return text;
}

bool int_to_byte(const Value& value, unsigned char& out, std::string& error) {
  if (value.tag != ValueTag::Int64 || value.as.i64 < 0 || value.as.i64 > 255) {
    error = "byte must be in range(0, 256)";
    return false;
  }
  out = static_cast<unsigned char>(value.as.i64);
  return true;
}

bool collect_byte_replacement(const Value& value, std::string& out, std::string& error) {
  const auto storage = binary_storage(value);
  if (storage.data != nullptr) {
    out.assign(storage.data, storage.size);
    return true;
  }
  Value iterator;
  if (!sequence_get_iter(value, iterator, error)) {
    return false;
  }
  while (true) {
    bool done = false;
    Value next;
    if (!sequence_iter_next(iterator, done, next, error)) {
      return false;
    }
    if (done) {
      return true;
    }
    unsigned char byte = 0;
    if (!int_to_byte(next, byte, error)) {
      return false;
    }
    out.push_back(static_cast<char>(byte));
  }
}

} // namespace

ListObject* value_as_list_storage(Value& value) {
  if (auto* list = value_as_list(value)) {
    return list;
  }
  auto* instance = value_as_instance(value);
  if (instance == nullptr) {
    return nullptr;
  }
  return value_as_list(instance->sequence_storage);
}

ListObject* value_as_mutable_list_storage(const Value& value) {
  if (auto* list = value_as_list(value)) {
    return list;
  }
  auto* instance = value_as_instance(value);
  if (instance == nullptr) {
    return nullptr;
  }
  return value_as_list(instance->sequence_storage);
}

const ListObject* value_as_list_storage(const Value& value) {
  if (auto* list = value_as_list(value)) {
    return list;
  }
  auto* instance = value_as_instance(value);
  if (instance == nullptr) {
    return nullptr;
  }
  return value_as_list(instance->sequence_storage);
}

Value Value::list(std::vector<Value> items) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_list_object();
  obj->items = std::move(items);
  v.as.obj = &obj->header;
  return v;
}

Value Value::list_reserved(size_t capacity) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_list_object();
  obj->items.clear();
  obj->items.reserve(capacity);
  v.as.obj = &obj->header;
  return v;
}

Value Value::range(int64_t start, int64_t stop, int64_t step) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_sequence_object<RangeObject>(ObjectKind::Range);
  obj->start = start;
  obj->stop = stop;
  obj->step = step;
  obj->int64_backed = true;
  obj->start_value = Value::int64(start);
  obj->stop_value = Value::int64(stop);
  obj->step_value = Value::int64(step);
  v.as.obj = &obj->header;
  return v;
}

Value Value::range_values(Value start, Value stop, Value step) {
  int64_t start_i64 = 0;
  int64_t stop_i64 = 0;
  int64_t step_i64 = 1;
  if (value_int_like_to_i64(start, start_i64) &&
      value_int_like_to_i64(stop, stop_i64) &&
      value_int_like_to_i64(step, step_i64)) {
    return Value::range(start_i64, stop_i64, step_i64);
  }
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_sequence_object<RangeObject>(ObjectKind::Range);
  obj->int64_backed = false;
  value_assign_fast(obj->start_value, start);
  value_assign_fast(obj->stop_value, stop);
  value_assign_fast(obj->step_value, step);
  v.as.obj = &obj->header;
  return v;
}

Value Value::range_iterator(int64_t current, int64_t stop, int64_t step) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_sequence_object<RangeIteratorObject>(ObjectKind::RangeIterator);
  obj->current = current;
  obj->stop = stop;
  obj->step = step;
  obj->int64_backed = true;
  obj->current_value = Value::int64(current);
  obj->stop_value = Value::int64(stop);
  obj->step_value = Value::int64(step);
  v.as.obj = &obj->header;
  return v;
}

Value Value::range_iterator_values(Value current, Value stop, Value step) {
  int64_t current_i64 = 0;
  int64_t stop_i64 = 0;
  int64_t step_i64 = 1;
  if (value_int_like_to_i64(current, current_i64) &&
      value_int_like_to_i64(stop, stop_i64) &&
      value_int_like_to_i64(step, step_i64)) {
    return Value::range_iterator(current_i64, stop_i64, step_i64);
  }
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_sequence_object<RangeIteratorObject>(ObjectKind::RangeIterator);
  obj->int64_backed = false;
  value_assign_fast(obj->current_value, current);
  value_assign_fast(obj->stop_value, stop);
  value_assign_fast(obj->step_value, step);
  v.as.obj = &obj->header;
  return v;
}

Value Value::sequence_iterator(Value source, uint64_t index) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_sequence_object<SequenceIteratorObject>(ObjectKind::SequenceIterator);
  obj->source = std::move(source);
  obj->index = index;
  v.as.obj = &obj->header;
  return v;
}

void sequence_release_object(Object* object) {
  switch (object->kind) {
    case ObjectKind::List:
      recycle_list_object(reinterpret_cast<ListObject*>(object));
      break;
    case ObjectKind::Range:
      delete reinterpret_cast<RangeObject*>(object);
      break;
    case ObjectKind::RangeIterator:
      delete reinterpret_cast<RangeIteratorObject*>(object);
      break;
    case ObjectKind::SequenceIterator:
      delete reinterpret_cast<SequenceIteratorObject*>(object);
      break;
    default:
      break;
  }
}

std::string sequence_to_string(const Value& value) {
  if (auto* list = value_as_list(value)) {
    return repr_items(list->items, "[", "]");
  }
  if (auto* range = value_as_range(value)) {
    if (range->int64_backed && range->start == 0 && range->step == 1) {
      return "range(" + std::to_string(range->stop) + ")";
    }
    if (!range->int64_backed) {
      return "range(" + value_to_string(range->start_value) + ", " +
             value_to_string(range->stop_value) + ", " + value_to_string(range->step_value) + ")";
    }
    return "range(" + std::to_string(range->start) + ", " +
           std::to_string(range->stop) + ", " + std::to_string(range->step) + ")";
  }
  if (value_as_range_iterator(value) != nullptr) {
    return "<range_iterator>";
  }
  if (value_as_sequence_iterator(value) != nullptr) {
    return "<sequence_iterator>";
  }
  return "<sequence>";
}

bool sequence_truthy(const Value& value) {
  if (auto* list = value_as_list(value)) {
    return !list->items.empty();
  }
  if (auto* range = value_as_range(value)) {
    if (!range->int64_backed) {
      Value compare;
      std::string error;
      const bool positive = value_int_like_compare(">", range->step_value, Value::int64(0), compare) &&
                            compare.tag == ValueTag::Bool && compare.as.b;
      if (positive) {
        return value_int_like_compare("<", range->start_value, range->stop_value, compare) &&
               compare.tag == ValueTag::Bool && compare.as.b;
      }
      return value_int_like_compare(">", range->start_value, range->stop_value, compare) &&
             compare.tag == ValueTag::Bool && compare.as.b;
    }
    return range->step > 0 ? range->start < range->stop : range->start > range->stop;
  }
  if (value_as_range_iterator(value) != nullptr) {
    return true;
  }
  if (value_as_sequence_iterator(value) != nullptr) {
    return true;
  }
  return true;
}

bool sequence_get_iter(const Value& iterable, Value& out, std::string& error) {
  if (auto* range = value_as_range(iterable)) {
    if (range->int64_backed) {
      out = Value::range_iterator(range->start, range->stop, range->step);
    } else {
      out = Value::range_iterator_values(range->start_value, range->stop_value, range->step_value);
    }
    return true;
  }
  if (value_as_dict(iterable) != nullptr || value_as_dict_view(iterable) != nullptr || value_as_module(iterable) != nullptr) {
    return mapping_get_iter(iterable, out, error);
  }
  if (value_as_set(iterable) != nullptr) {
    return set_get_iter(iterable, out, error);
  }
  if (value_as_generator(iterable) != nullptr) {
    return generator_get_iter(iterable, out, error);
  }
  if (value_as_range_iterator(iterable) != nullptr || value_as_sequence_iterator(iterable) != nullptr) {
    value_assign_fast(out, iterable);
    return true;
  }
  if (iterable.tag == ValueTag::Object && iterable.as.obj != nullptr && iterable.as.obj->kind == ObjectKind::File) {
    auto* file = reinterpret_cast<FileObject*>(iterable.as.obj);
    if (file->closed) {
      error = "file.__iter__ on closed file";
      return false;
    }
    value_assign_fast(out, iterable);
    return true;
  }
  if (value_is_functional_iterator(iterable)) {
    value_assign_fast(out, iterable);
    return true;
  }
  if (value_as_list(iterable) != nullptr ||
      (iterable.tag == ValueTag::Object && iterable.as.obj != nullptr &&
       (iterable.as.obj->kind == ObjectKind::Tuple ||
        iterable.as.obj->kind == ObjectKind::String ||
        iterable.as.obj->kind == ObjectKind::Bytes ||
        iterable.as.obj->kind == ObjectKind::ByteArray ||
        iterable.as.obj->kind == ObjectKind::MemoryView))) {
    out = Value::sequence_iterator(iterable, 0);
    return true;
  }
  Value struct_tuple;
  if (struct_sequence_storage(iterable, struct_tuple)) {
    out = Value::sequence_iterator(struct_tuple, 0);
    return true;
  }
  if (auto* instance = value_as_instance(iterable)) {
    if (value_as_list(instance->sequence_storage) != nullptr) {
      out = Value::sequence_iterator(instance->sequence_storage, 0);
      return true;
    }
    if (value_as_dict(instance->mapping_storage) != nullptr) {
      return mapping_get_iter(instance->mapping_storage, out, error);
    }
    Value data;
    std::string ignored;
    if (object_get_attr(iterable, "data", data, ignored)) {
      if (sequence_get_iter(data, out, ignored)) {
        error.clear();
        return true;
      }
    }
  }
  error = "object is not iterable";
  return false;
}

bool sequence_iter_next(Value& iterator, bool& done, Value& out, std::string& error) {
  if (auto* range = value_as_range_iterator(iterator)) {
    if (!range->int64_backed) {
      Value compare;
      std::string cmp_error;
      const bool positive = value_int_like_compare(">", range->step_value, Value::int64(0), compare) &&
                            compare.tag == ValueTag::Bool && compare.as.b;
      const char* op = positive ? ">=" : "<=";
      if (!value_compare(op, range->current_value, range->stop_value, compare, cmp_error)) {
        error = cmp_error;
        return false;
      }
      done = compare.tag == ValueTag::Bool && compare.as.b;
      if (done) {
        value_set_none(out);
        return true;
      }
      value_assign_fast(out, range->current_value);
      Value next;
      if (!value_add(range->current_value, range->step_value, next, error)) {
        return false;
      }
      value_assign_fast(range->current_value, next);
      return true;
    }
    done = range->step > 0 ? range->current >= range->stop : range->current <= range->stop;
    if (done) {
      value_set_none(out);
      return true;
    }
    value_set_int64(out, range->current);
    range->current += range->step;
    return true;
  }
  if (auto* seq = value_as_sequence_iterator(iterator)) {
    Value index = Value::int64(static_cast<int64_t>(seq->index));
    if (!sequence_get_item(seq->source, index, out, error)) {
      if (error == "index out of range") {
        error.clear();
        done = true;
        value_set_none(out);
        return true;
      }
      return false;
    }
    ++seq->index;
    done = false;
    return true;
  }
  if (value_as_dict_iterator(iterator) != nullptr) {
    return mapping_iter_next(iterator, done, out, error);
  }
  if (value_as_set_iterator(iterator) != nullptr) {
    return set_iter_next(iterator, done, out, error);
  }
  if (value_as_generator(iterator) != nullptr) {
    return generator_iter_next(iterator, done, out, error);
  }
  if (iterator.tag == ValueTag::Object && iterator.as.obj != nullptr && iterator.as.obj->kind == ObjectKind::File) {
    auto* file = reinterpret_cast<FileObject*>(iterator.as.obj);
    if (file->closed) {
      error = "file.__next__ on closed file";
      return false;
    }
    const size_t start = std::min(file->cursor, file->buffer.size());
    if (start >= file->buffer.size()) {
      done = true;
      value_set_none(out);
      return true;
    }
    size_t end = start;
    while (end < file->buffer.size()) {
      ++end;
      if (file->buffer[end - 1] == '\n') {
        break;
      }
    }
    std::string line = file->buffer.substr(start, end - start);
    out = file->binary ? Value::bytes(std::move(line)) : Value::string(std::move(line));
    file->cursor = end;
    done = false;
    return true;
  }
  if (value_is_functional_iterator(iterator)) {
    return functional_iterator_next(iterator, done, out, error);
  }
  error = "invalid iterator";
  return false;
}

bool sequence_list_append(Value& list, const Value& item, std::string& error) {
  auto* obj = value_as_list_storage(list);
  if (obj == nullptr) {
    error = "list append target is not a list: " + value_to_repr(list);
    return false;
  }
  obj->items.push_back(item);
  return true;
}

bool sequence_get_item(const Value& object, const Value& index, Value& out, std::string& error) {
  if (value_as_class(object) != nullptr) {
    Value args;
    if (value_as_tuple(index) != nullptr) {
      value_assign_fast(args, index);
    } else {
      args = Value::tuple({index});
    }
    out = Value::generic_alias(object, std::move(args));
    return true;
  }
  if (auto* range = value_as_range(object)) {
    const int64_t length = range_length(range->start, range->stop, range->step);
    if (auto* slice = value_as_slice(index)) {
      int64_t start = 0;
      int64_t stop = 0;
      int64_t step = 1;
      if (!normalize_slice(*slice, length, start, stop, step, error)) {
        return false;
      }
      const int64_t new_start = range->start + start * range->step;
      const int64_t new_step = range->step * step;
      const int64_t new_length = range_length(start, stop, step);
      const int64_t new_stop = new_start + new_step * new_length;
      out = Value::range(new_start, new_stop, new_step);
      return true;
    }
    if (index.tag != ValueTag::Int64) {
      error = "sequence index must be int";
      return false;
    }
    uint64_t resolved = 0;
    if (!normalize_index(index.as.i64, static_cast<uint64_t>(length), resolved)) {
      error = "index out of range";
      return false;
    }
    out = Value::int64(range->start + static_cast<int64_t>(resolved) * range->step);
    return true;
  }
  if (auto* list = value_as_list(object)) {
    if (auto* slice = value_as_slice(index)) {
      int64_t start = 0;
      int64_t stop = 0;
      int64_t step = 1;
      if (!normalize_slice(*slice, static_cast<int64_t>(list->items.size()), start, stop, step, error)) {
        return false;
      }
      std::vector<Value> items;
      if (step > 0) {
        for (int64_t i = start; i < stop; i += step) {
          items.push_back(list->items[static_cast<size_t>(i)]);
        }
      } else {
        for (int64_t i = start; i > stop; i += step) {
          items.push_back(list->items[static_cast<size_t>(i)]);
        }
      }
      out = Value::list(std::move(items));
      return true;
    }
    if (index.tag != ValueTag::Int64) {
      error = "sequence index must be int";
      return false;
    }
    uint64_t resolved = 0;
    if (!normalize_index(index.as.i64, static_cast<uint64_t>(list->items.size()), resolved)) {
      error = "index out of range";
      return false;
    }
    value_assign_fast(out, list->items[static_cast<size_t>(resolved)]);
    return true;
  }
  if (value_as_dict(object) != nullptr || value_as_module(object) != nullptr) {
    return mapping_get_item(object, index, out, error);
  }
  if (auto* instance = value_as_instance(object)) {
    Value struct_tuple;
    if (struct_sequence_storage(object, struct_tuple)) {
      return sequence_get_item(struct_tuple, index, out, error);
    }
    if (value_as_dict(instance->mapping_storage) != nullptr) {
      return mapping_get_item(instance->mapping_storage, index, out, error);
    }
    if (value_as_list(instance->sequence_storage) != nullptr) {
      return sequence_get_item(instance->sequence_storage, index, out, error);
    }
  }
  if (object.tag == ValueTag::Object && object.as.obj != nullptr && object.as.obj->kind == ObjectKind::Tuple) {
    auto* tuple = reinterpret_cast<TupleObject*>(object.as.obj);
    if (auto* slice = value_as_slice(index)) {
      int64_t start = 0;
      int64_t stop = 0;
      int64_t step = 1;
      if (!normalize_slice(*slice, static_cast<int64_t>(tuple->items.size()), start, stop, step, error)) {
        return false;
      }
      std::vector<Value> items;
      if (step > 0) {
        for (int64_t i = start; i < stop; i += step) {
          items.push_back(tuple->items[static_cast<size_t>(i)]);
        }
      } else {
        for (int64_t i = start; i > stop; i += step) {
          items.push_back(tuple->items[static_cast<size_t>(i)]);
        }
      }
      out = Value::tuple(std::move(items));
      return true;
    }
    if (index.tag != ValueTag::Int64) {
      error = "sequence index must be int";
      return false;
    }
    uint64_t resolved = 0;
    if (!normalize_index(index.as.i64, static_cast<uint64_t>(tuple->items.size()), resolved)) {
      error = "index out of range";
      return false;
    }
    value_assign_fast(out, tuple->items[static_cast<size_t>(resolved)]);
    return true;
  }
  if (object.tag == ValueTag::Object && object.as.obj != nullptr && object.as.obj->kind == ObjectKind::String) {
    auto* string = reinterpret_cast<StringObject*>(object.as.obj);
    const auto view = string_object_view(*string);
    const auto codepoint_count = utf8_codepoint_count(view);
    if (auto* slice = value_as_slice(index)) {
      int64_t start = 0;
      int64_t stop = 0;
      int64_t step = 1;
      if (!normalize_slice(*slice, static_cast<int64_t>(codepoint_count), start, stop, step, error)) {
        return false;
      }
      out = Value::string(utf8_slice_text(view, start, stop, step));
      return true;
    }
    if (index.tag != ValueTag::Int64) {
      error = "sequence index must be int";
      return false;
    }
    uint64_t resolved = 0;
    if (!normalize_index(index.as.i64, static_cast<uint64_t>(codepoint_count), resolved)) {
      error = "index out of range";
      return false;
    }
    const auto ch = utf8_codepoint_at(view, static_cast<size_t>(resolved));
    out = Value::string_view(ch);
    return true;
  }
  if (object.tag == ValueTag::Object && object.as.obj != nullptr && object.as.obj->kind == ObjectKind::Bytes) {
    auto* bytes = reinterpret_cast<BytesObject*>(object.as.obj);
    const auto view = bytes_object_view(*bytes);
    if (auto* slice = value_as_slice(index)) {
      int64_t start = 0;
      int64_t stop = 0;
      int64_t step = 1;
      if (!normalize_slice(*slice, static_cast<int64_t>(view.size()), start, stop, step, error)) {
        return false;
      }
      out = Value::bytes(binary_slice_text(view, start, stop, step));
      return true;
    }
    if (index.tag != ValueTag::Int64) {
      error = "sequence index must be int";
      return false;
    }
    uint64_t resolved = 0;
    if (!normalize_index(index.as.i64, static_cast<uint64_t>(view.size()), resolved)) {
      error = "index out of range";
      return false;
    }
    value_set_int64(out, static_cast<unsigned char>(view[static_cast<size_t>(resolved)]));
    return true;
  }
  if (object.tag == ValueTag::Object && object.as.obj != nullptr &&
      (object.as.obj->kind == ObjectKind::ByteArray || object.as.obj->kind == ObjectKind::MemoryView)) {
    const auto storage = binary_storage(object);
    if (storage.data == nullptr) {
      error = "invalid binary object";
      return false;
    }
    const std::string_view storage_view(storage.data, storage.size);
    if (auto* slice = value_as_slice(index)) {
      int64_t start = 0;
      int64_t stop = 0;
      int64_t step = 1;
      if (!normalize_slice(*slice, static_cast<int64_t>(storage.size), start, stop, step, error)) {
        return false;
      }
      if (object.as.obj->kind == ObjectKind::MemoryView && step == 1) {
        const auto normalized_size = stop >= start ? static_cast<size_t>(stop - start) : 0;
        out = Value::memoryview(
            reinterpret_cast<MemoryViewObject*>(object.as.obj)->owner,
            reinterpret_cast<MemoryViewObject*>(object.as.obj)->offset + static_cast<size_t>(start),
            normalized_size,
            storage.readonly);
      } else {
        auto text = binary_slice_text(storage_view, start, stop, step);
        out = object.as.obj->kind == ObjectKind::ByteArray ? Value::bytearray(std::move(text)) : Value::bytes(std::move(text));
      }
      return true;
    }
    const Value* actual_index = &index;
    if (object.as.obj->kind == ObjectKind::MemoryView) {
      if (auto* tuple = value_as_tuple(index)) {
        if (tuple->items.size() != 1) {
          error = "memoryview: invalid tuple index";
          return false;
        }
        actual_index = &tuple->items[0];
      }
    }
    if (actual_index->tag != ValueTag::Int64) {
      error = "sequence index must be int";
      return false;
    }
    size_t logical_size = storage.size;
    size_t itemsize = 1;
    const MemoryViewObject* memory_view = nullptr;
    if (object.as.obj->kind == ObjectKind::MemoryView) {
      memory_view = reinterpret_cast<const MemoryViewObject*>(object.as.obj);
      itemsize = memoryview_format_itemsize(memory_view->format);
      if (itemsize == 0 || itemsize > storage.size || (storage.size % itemsize) != 0) {
        error = "unsupported memoryview format";
        return false;
      }
      logical_size = storage.size / itemsize;
    }
    uint64_t resolved = 0;
    if (!normalize_index(actual_index->as.i64, static_cast<uint64_t>(logical_size), resolved)) {
      error = "index out of range";
      return false;
    }
    const size_t byte_offset = static_cast<size_t>(resolved) * itemsize;
    uint64_t value = 0;
    for (size_t i = 0; i < itemsize; ++i) {
      value |= static_cast<uint64_t>(static_cast<unsigned char>(storage.data[byte_offset + i])) << (i * 8u);
    }
    value_set_int64(out, static_cast<int64_t>(value));
    return true;
  }
  if (instance_get_native_data(object, "typing._Alias") != nullptr) {
    value_assign_fast(out, object);
    return true;
  }
  if (value_as_class(object) != nullptr ||
      value_as_function(object) != nullptr ||
      value_as_native_function(object) != nullptr) {
    value_assign_fast(out, object);
    return true;
  }
  error = "object is not subscriptable";
  return false;
}

bool sequence_set_item(Value& object, const Value& index, const Value& item, std::string& error) {
  if (auto* list = value_as_list(object)) {
    if (auto* slice = value_as_slice(index)) {
      int64_t start = 0;
      int64_t stop = 0;
      int64_t step = 1;
      if (!normalize_slice(*slice, static_cast<int64_t>(list->items.size()), start, stop, step, error)) {
        return false;
      }
      std::vector<Value> replacement;
      if (auto* replacement_list = value_as_list(item)) {
        replacement = replacement_list->items;
      } else if (auto* replacement_tuple = value_as_tuple(item)) {
        replacement = replacement_tuple->items;
      } else {
        Value iterator;
        if (!sequence_get_iter(item, iterator, error)) {
          return false;
        }
        while (true) {
          bool done = false;
          Value next;
          if (!sequence_iter_next(iterator, done, next, error)) {
            return false;
          }
          if (done) {
            break;
          }
          replacement.push_back(std::move(next));
        }
      }
      if (step == 1) {
        list->items.erase(
            list->items.begin() + static_cast<std::ptrdiff_t>(start),
            list->items.begin() + static_cast<std::ptrdiff_t>(stop));
        list->items.insert(
            list->items.begin() + static_cast<std::ptrdiff_t>(start),
            replacement.begin(),
            replacement.end());
        return true;
      }
      std::vector<size_t> indexes;
      if (step > 0) {
        for (int64_t i = start; i < stop; i += step) {
          indexes.push_back(static_cast<size_t>(i));
        }
      } else {
        for (int64_t i = start; i > stop; i += step) {
          indexes.push_back(static_cast<size_t>(i));
        }
      }
      if (indexes.size() != replacement.size()) {
        error = "attempt to assign sequence of size " + std::to_string(replacement.size()) +
                " to extended slice of size " + std::to_string(indexes.size());
        return false;
      }
      for (size_t i = 0; i < indexes.size(); ++i) {
        value_assign_fast(list->items[indexes[i]], replacement[i]);
      }
      return true;
    }
    if (index.tag != ValueTag::Int64) {
      error = "sequence index must be int";
      return false;
    }
    uint64_t resolved = 0;
    if (!normalize_index(index.as.i64, static_cast<uint64_t>(list->items.size()), resolved)) {
      error = "index out of range";
      return false;
    }
    list->items[static_cast<size_t>(resolved)] = item;
    return true;
  }
  if (value_as_dict(object) != nullptr || value_as_module(object) != nullptr) {
    return mapping_set_item(object, index, item, error);
  }
  if (auto* instance = value_as_instance(object)) {
    if (value_as_dict(instance->mapping_storage) != nullptr) {
      return mapping_set_item(instance->mapping_storage, index, item, error);
    }
    if (value_as_list(instance->sequence_storage) != nullptr) {
      return sequence_set_item(instance->sequence_storage, index, item, error);
    }
  }
  if (auto* bytearray = value_as_bytearray(object)) {
    if (auto* slice = value_as_slice(index)) {
      int64_t start = 0;
      int64_t stop = 0;
      int64_t step = 1;
      if (!normalize_slice(*slice, static_cast<int64_t>(bytearray->value.size()), start, stop, step, error)) {
        return false;
      }
      std::string replacement;
      if (!collect_byte_replacement(item, replacement, error)) {
        return false;
      }
      if (step == 1) {
        bytearray->value.erase(
            bytearray->value.begin() + static_cast<std::ptrdiff_t>(start),
            bytearray->value.begin() + static_cast<std::ptrdiff_t>(stop));
        bytearray->value.insert(
            bytearray->value.begin() + static_cast<std::ptrdiff_t>(start),
            replacement.begin(),
            replacement.end());
        return true;
      }
      std::vector<size_t> indexes;
      if (step > 0) {
        for (int64_t i = start; i < stop; i += step) {
          indexes.push_back(static_cast<size_t>(i));
        }
      } else {
        for (int64_t i = start; i > stop; i += step) {
          indexes.push_back(static_cast<size_t>(i));
        }
      }
      if (indexes.size() != replacement.size()) {
        error = "attempt to assign bytes of size " + std::to_string(replacement.size()) +
                " to extended slice of size " + std::to_string(indexes.size());
        return false;
      }
      for (size_t i = 0; i < indexes.size(); ++i) {
        bytearray->value[indexes[i]] = replacement[i];
      }
      return true;
    }
    if (index.tag != ValueTag::Int64) {
      error = "sequence index must be int";
      return false;
    }
    uint64_t resolved = 0;
    if (!normalize_index(index.as.i64, static_cast<uint64_t>(bytearray->value.size()), resolved)) {
      error = "index out of range";
      return false;
    }
    unsigned char byte = 0;
    if (!int_to_byte(item, byte, error)) {
      return false;
    }
    bytearray->value[static_cast<size_t>(resolved)] = static_cast<char>(byte);
    return true;
  }
  if (auto* view = value_as_memoryview(object)) {
    if (view->readonly) {
      error = "cannot modify read-only memory";
      return false;
    }
    if (view->released) {
      error = "operation forbidden on released memoryview object";
      return false;
    }
    auto* bytearray = value_as_bytearray(view->owner);
    if (bytearray == nullptr) {
      error = "memoryview owner is not writable";
      return false;
    }
    if (auto* slice = value_as_slice(index)) {
      int64_t start = 0;
      int64_t stop = 0;
      int64_t step = 1;
      if (!normalize_slice(*slice, static_cast<int64_t>(view->size), start, stop, step, error)) {
        return false;
      }
      std::string replacement;
      if (!collect_byte_replacement(item, replacement, error)) {
        return false;
      }
      std::vector<size_t> indexes;
      if (step > 0) {
        for (int64_t i = start; i < stop; i += step) {
          indexes.push_back(static_cast<size_t>(i));
        }
      } else {
        for (int64_t i = start; i > stop; i += step) {
          indexes.push_back(static_cast<size_t>(i));
        }
      }
      if (indexes.size() != replacement.size()) {
        error = "memoryview assignment requires same-sized bytes-like object";
        return false;
      }
      for (size_t i = 0; i < indexes.size(); ++i) {
        bytearray->value[view->offset + indexes[i]] = replacement[i];
      }
      return true;
    }
    const Value* actual_index = &index;
    if (auto* tuple = value_as_tuple(index)) {
      if (tuple->items.size() != 1) {
        error = "memoryview: invalid tuple index";
        return false;
      }
      actual_index = &tuple->items[0];
    }
    if (actual_index->tag != ValueTag::Int64) {
      error = "sequence index must be int";
      return false;
    }
    uint64_t resolved = 0;
    if (!normalize_index(actual_index->as.i64, static_cast<uint64_t>(view->size), resolved)) {
      error = "index out of range";
      return false;
    }
    unsigned char byte = 0;
    if (!int_to_byte(item, byte, error)) {
      return false;
    }
    bytearray->value[view->offset + static_cast<size_t>(resolved)] = static_cast<char>(byte);
    return true;
  }
  error = "object does not support item assignment";
  return false;
}

bool sequence_delete_item(Value& object, const Value& index, std::string& error) {
  if (auto* list = value_as_list(object)) {
    if (auto* slice = value_as_slice(index)) {
      int64_t start = 0;
      int64_t stop = 0;
      int64_t step = 1;
      if (!normalize_slice(*slice, static_cast<int64_t>(list->items.size()), start, stop, step, error)) {
        return false;
      }
      if (step == 1) {
        if (start < stop) {
          list->items.erase(
              list->items.begin() + static_cast<std::ptrdiff_t>(start),
              list->items.begin() + static_cast<std::ptrdiff_t>(stop));
        }
        return true;
      }
      std::vector<size_t> indexes;
      if (step > 0) {
        for (int64_t i = start; i < stop; i += step) {
          indexes.push_back(static_cast<size_t>(i));
        }
      } else {
        for (int64_t i = start; i > stop; i += step) {
          indexes.push_back(static_cast<size_t>(i));
        }
      }
      std::sort(indexes.begin(), indexes.end(), [](size_t lhs, size_t rhs) { return lhs > rhs; });
      for (const auto index_to_delete : indexes) {
        list->items.erase(list->items.begin() + static_cast<std::ptrdiff_t>(index_to_delete));
      }
      return true;
    }
    if (index.tag != ValueTag::Int64) {
      error = "sequence index must be int";
      return false;
    }
    uint64_t resolved = 0;
    if (!normalize_index(index.as.i64, static_cast<uint64_t>(list->items.size()), resolved)) {
      error = "index out of range";
      return false;
    }
    list->items.erase(list->items.begin() + static_cast<std::ptrdiff_t>(resolved));
    return true;
  }
  if (value_as_dict(object) != nullptr || value_as_module(object) != nullptr) {
    return mapping_delete_item(object, index, error);
  }
  if (auto* instance = value_as_instance(object)) {
    if (value_as_dict(instance->mapping_storage) != nullptr) {
      return mapping_delete_item(instance->mapping_storage, index, error);
    }
  }
  if (auto* bytearray = value_as_bytearray(object)) {
    if (auto* slice = value_as_slice(index)) {
      int64_t start = 0;
      int64_t stop = 0;
      int64_t step = 1;
      if (!normalize_slice(*slice, static_cast<int64_t>(bytearray->value.size()), start, stop, step, error)) {
        return false;
      }
      if (step == 1) {
        if (start < stop) {
          bytearray->value.erase(
              bytearray->value.begin() + static_cast<std::ptrdiff_t>(start),
              bytearray->value.begin() + static_cast<std::ptrdiff_t>(stop));
        }
        return true;
      }
      std::vector<size_t> indexes;
      if (step > 0) {
        for (int64_t i = start; i < stop; i += step) {
          indexes.push_back(static_cast<size_t>(i));
        }
      } else {
        for (int64_t i = start; i > stop; i += step) {
          indexes.push_back(static_cast<size_t>(i));
        }
      }
      std::sort(indexes.begin(), indexes.end(), [](size_t lhs, size_t rhs) { return lhs > rhs; });
      for (const auto index_to_delete : indexes) {
        bytearray->value.erase(bytearray->value.begin() + static_cast<std::ptrdiff_t>(index_to_delete));
      }
      return true;
    }
    if (index.tag != ValueTag::Int64) {
      error = "sequence index must be int";
      return false;
    }
    uint64_t resolved = 0;
    if (!normalize_index(index.as.i64, static_cast<uint64_t>(bytearray->value.size()), resolved)) {
      error = "index out of range";
      return false;
    }
    bytearray->value.erase(bytearray->value.begin() + static_cast<std::ptrdiff_t>(resolved));
    return true;
  }
  error = "object does not support item deletion";
  return false;
}

bool sequence_len(const Value& value, Value& out, std::string& error) {
  if (auto* list = value_as_list(value)) {
    value_set_int64(out, static_cast<int64_t>(list->items.size()));
    return true;
  }
  if (value.tag == ValueTag::Object && value.as.obj != nullptr && value.as.obj->kind == ObjectKind::Tuple) {
    auto* tuple = reinterpret_cast<TupleObject*>(value.as.obj);
    value_set_int64(out, static_cast<int64_t>(tuple->items.size()));
    return true;
  }
  if (value.tag == ValueTag::Object && value.as.obj != nullptr && value.as.obj->kind == ObjectKind::String) {
    auto* string = reinterpret_cast<StringObject*>(value.as.obj);
    value_set_int64(out, static_cast<int64_t>(utf8_codepoint_count(string_object_view(*string))));
    return true;
  }
  if (value.tag == ValueTag::Object && value.as.obj != nullptr && value.as.obj->kind == ObjectKind::Bytes) {
    auto* bytes = reinterpret_cast<BytesObject*>(value.as.obj);
    value_set_int64(out, static_cast<int64_t>(bytes->size));
    return true;
  }
  if (auto* range = value_as_range(value)) {
    if (!range->int64_backed) {
      error = "range length requires int-backed range";
      return false;
    }
    value_set_int64(out, range_length(range->start, range->stop, range->step));
    return true;
  }
  if (auto* bytearray = value_as_bytearray(value)) {
    value_set_int64(out, static_cast<int64_t>(bytearray->value.size()));
    return true;
  }
  if (auto* view = value_as_memoryview(value)) {
    if (view->released) {
      error = "operation forbidden on released memoryview object";
      return false;
    }
    value_set_int64(out, static_cast<int64_t>(memoryview_item_count(*view)));
    return true;
  }
  if (value_as_dict(value) != nullptr || value_as_dict_view(value) != nullptr || value_as_module(value) != nullptr) {
    return mapping_len(value, out, error);
  }
  if (auto* instance = value_as_instance(value)) {
    Value struct_tuple;
    if (struct_sequence_storage(value, struct_tuple)) {
      return sequence_len(struct_tuple, out, error);
    }
    Value bytes_payload;
    std::string ignored;
    if (object_get_attr(value, "__xlang3_bytes_value__", bytes_payload, ignored)) {
      if (auto* bytes = value_as_bytes(bytes_payload)) {
        value_set_int64(out, static_cast<int64_t>(bytes->size));
        return true;
      }
    }
    if (value_as_dict(instance->mapping_storage) != nullptr) {
      return mapping_len(instance->mapping_storage, out, error);
    }
    if (value_as_list(instance->sequence_storage) != nullptr) {
      return sequence_len(instance->sequence_storage, out, error);
    }
  }
  if (value_as_set(value) != nullptr) {
    return set_len(value, out, error);
  }
  error = "object has no len()";
  return false;
}

} // namespace xlang3
