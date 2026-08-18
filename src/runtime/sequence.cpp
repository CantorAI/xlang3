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

#include "xlang3/generator.h"
#include "xlang3/mapping.h"
#include "xlang3/set_object.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace xlang3 {

namespace {

template <typename T>
T* allocate_sequence_object(ObjectKind kind) {
  auto* obj = new T();
  obj->header.kind = kind;
  obj->header.refcnt = 1;
  return obj;
}

std::string repr_items(const std::vector<Value>& items, const char* open, const char* close) {
  std::string text = open;
  for (size_t i = 0; i < items.size(); ++i) {
    if (i != 0) {
      text += ", ";
    }
    text += value_to_string(items[i]);
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

const std::string* binary_storage(const Value& value, size_t& offset, size_t& size, bool& readonly) {
  offset = 0;
  size = 0;
  readonly = true;
  if (auto* bytes = value_as_bytes(value)) {
    size = bytes->value.size();
    return &bytes->value;
  }
  if (auto* bytearray = value_as_bytearray(value)) {
    size = bytearray->value.size();
    readonly = false;
    return &bytearray->value;
  }
  if (auto* view = value_as_memoryview(value)) {
    size_t owner_offset = 0;
    size_t owner_size = 0;
    bool owner_readonly = true;
    const auto* storage = binary_storage(view->owner, owner_offset, owner_size, owner_readonly);
    if (storage == nullptr || view->offset > owner_size) {
      return nullptr;
    }
    offset = owner_offset + view->offset;
    size = view->size;
    readonly = view->readonly || owner_readonly;
    return storage;
  }
  return nullptr;
}

std::string binary_slice_text(const std::string& storage, size_t offset, int64_t start, int64_t stop, int64_t step) {
  std::string text;
  if (step > 0) {
    for (int64_t i = start; i < stop; i += step) {
      text.push_back(storage[offset + static_cast<size_t>(i)]);
    }
  } else {
    for (int64_t i = start; i > stop; i += step) {
      text.push_back(storage[offset + static_cast<size_t>(i)]);
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

} // namespace

Value Value::list(std::vector<Value> items) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_sequence_object<ListObject>(ObjectKind::List);
  obj->items = std::move(items);
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
      delete reinterpret_cast<ListObject*>(object);
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
    if (range->start == 0 && range->step == 1) {
      return "range(" + std::to_string(range->stop) + ")";
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
    out = Value::range_iterator(range->start, range->stop, range->step);
    return true;
  }
  if (value_as_dict(iterable) != nullptr) {
    return mapping_get_iter(iterable, out, error);
  }
  if (value_as_set(iterable) != nullptr) {
    return set_get_iter(iterable, out, error);
  }
  if (value_as_generator(iterable) != nullptr) {
    return generator_get_iter(iterable, out, error);
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
  error = "object is not iterable";
  return false;
}

bool sequence_iter_next(Value& iterator, bool& done, Value& out, std::string& error) {
  if (auto* range = value_as_range_iterator(iterator)) {
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
  error = "invalid iterator";
  return false;
}

bool sequence_list_append(Value& list, const Value& item, std::string& error) {
  auto* obj = value_as_list(list);
  if (obj == nullptr) {
    error = "list append target is not a list";
    return false;
  }
  obj->items.push_back(item);
  return true;
}

bool sequence_get_item(const Value& object, const Value& index, Value& out, std::string& error) {
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
  if (value_as_dict(object) != nullptr) {
    return mapping_get_item(object, index, out, error);
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
    if (auto* slice = value_as_slice(index)) {
      int64_t start = 0;
      int64_t stop = 0;
      int64_t step = 1;
      if (!normalize_slice(*slice, static_cast<int64_t>(string->value.size()), start, stop, step, error)) {
        return false;
      }
      std::string text;
      if (step > 0) {
        for (int64_t i = start; i < stop; i += step) {
          text.push_back(string->value[static_cast<size_t>(i)]);
        }
      } else {
        for (int64_t i = start; i > stop; i += step) {
          text.push_back(string->value[static_cast<size_t>(i)]);
        }
      }
      out = Value::string(std::move(text));
      return true;
    }
    if (index.tag != ValueTag::Int64) {
      error = "sequence index must be int";
      return false;
    }
    uint64_t resolved = 0;
    if (!normalize_index(index.as.i64, static_cast<uint64_t>(string->value.size()), resolved)) {
      error = "index out of range";
      return false;
    }
    out = Value::string(std::string(1, string->value[static_cast<size_t>(resolved)]));
    return true;
  }
  if (object.tag == ValueTag::Object && object.as.obj != nullptr && object.as.obj->kind == ObjectKind::Bytes) {
    auto* bytes = reinterpret_cast<BytesObject*>(object.as.obj);
    if (auto* slice = value_as_slice(index)) {
      int64_t start = 0;
      int64_t stop = 0;
      int64_t step = 1;
      if (!normalize_slice(*slice, static_cast<int64_t>(bytes->value.size()), start, stop, step, error)) {
        return false;
      }
      std::string text;
      if (step > 0) {
        for (int64_t i = start; i < stop; i += step) {
          text.push_back(bytes->value[static_cast<size_t>(i)]);
        }
      } else {
        for (int64_t i = start; i > stop; i += step) {
          text.push_back(bytes->value[static_cast<size_t>(i)]);
        }
      }
      out = Value::bytes(std::move(text));
      return true;
    }
    if (index.tag != ValueTag::Int64) {
      error = "sequence index must be int";
      return false;
    }
    uint64_t resolved = 0;
    if (!normalize_index(index.as.i64, static_cast<uint64_t>(bytes->value.size()), resolved)) {
      error = "index out of range";
      return false;
    }
    value_set_int64(out, static_cast<unsigned char>(bytes->value[static_cast<size_t>(resolved)]));
    return true;
  }
  if (object.tag == ValueTag::Object && object.as.obj != nullptr &&
      (object.as.obj->kind == ObjectKind::ByteArray || object.as.obj->kind == ObjectKind::MemoryView)) {
    size_t offset = 0;
    size_t size = 0;
    bool readonly = true;
    const auto* storage = binary_storage(object, offset, size, readonly);
    if (storage == nullptr) {
      error = "invalid binary object";
      return false;
    }
    if (auto* slice = value_as_slice(index)) {
      int64_t start = 0;
      int64_t stop = 0;
      int64_t step = 1;
      if (!normalize_slice(*slice, static_cast<int64_t>(size), start, stop, step, error)) {
        return false;
      }
      if (object.as.obj->kind == ObjectKind::MemoryView && step == 1) {
        const auto normalized_size = stop >= start ? static_cast<size_t>(stop - start) : 0;
        out = Value::memoryview(
            reinterpret_cast<MemoryViewObject*>(object.as.obj)->owner,
            offset + static_cast<size_t>(start),
            normalized_size,
            readonly);
      } else {
        auto text = binary_slice_text(*storage, offset, start, stop, step);
        out = object.as.obj->kind == ObjectKind::ByteArray ? Value::bytearray(std::move(text)) : Value::bytes(std::move(text));
      }
      return true;
    }
    if (index.tag != ValueTag::Int64) {
      error = "sequence index must be int";
      return false;
    }
    uint64_t resolved = 0;
    if (!normalize_index(index.as.i64, static_cast<uint64_t>(size), resolved)) {
      error = "index out of range";
      return false;
    }
    value_set_int64(out, static_cast<unsigned char>((*storage)[offset + static_cast<size_t>(resolved)]));
    return true;
  }
  error = "object is not subscriptable";
  return false;
}

bool sequence_set_item(Value& object, const Value& index, const Value& item, std::string& error) {
  if (auto* list = value_as_list(object)) {
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
  if (value_as_dict(object) != nullptr) {
    return mapping_set_item(object, index, item, error);
  }
  if (auto* bytearray = value_as_bytearray(object)) {
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
    auto* bytearray = value_as_bytearray(view->owner);
    if (bytearray == nullptr) {
      error = "memoryview owner is not writable";
      return false;
    }
    if (index.tag != ValueTag::Int64) {
      error = "sequence index must be int";
      return false;
    }
    uint64_t resolved = 0;
    if (!normalize_index(index.as.i64, static_cast<uint64_t>(view->size), resolved)) {
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
  if (value_as_dict(object) != nullptr) {
    return mapping_delete_item(object, index, error);
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
    value_set_int64(out, static_cast<int64_t>(string->value.size()));
    return true;
  }
  if (value.tag == ValueTag::Object && value.as.obj != nullptr && value.as.obj->kind == ObjectKind::Bytes) {
    auto* bytes = reinterpret_cast<BytesObject*>(value.as.obj);
    value_set_int64(out, static_cast<int64_t>(bytes->value.size()));
    return true;
  }
  if (auto* bytearray = value_as_bytearray(value)) {
    value_set_int64(out, static_cast<int64_t>(bytearray->value.size()));
    return true;
  }
  if (auto* view = value_as_memoryview(value)) {
    value_set_int64(out, static_cast<int64_t>(view->size));
    return true;
  }
  if (value_as_dict(value) != nullptr) {
    return mapping_len(value, out, error);
  }
  if (value_as_set(value) != nullptr) {
    return set_len(value, out, error);
  }
  error = "object has no len()";
  return false;
}

} // namespace xlang3
