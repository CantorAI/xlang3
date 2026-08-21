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
#include "xlang3/value.h"

#include "xlang3/generator.h"
#include "xlang3/functional_iterators.h"
#include "xlang3/mapping.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/perf_counters.h"
#include "xlang3/sequence.h"
#include "xlang3/set_object.h"
#include "xlang3/value_hash.h"

#include "runtime/memory/x3_runtime_memory.h"

#include <cmath>
#include <array>
#include <cstring>
#include <limits>
#include <new>
#include <vector>
#if defined(XLANG3_EMBEDDED)
#include <cstdio>
#else
#include <sstream>
#endif

namespace xlang3 {

namespace {

template <typename T>
T* allocate_object(ObjectKind kind) {
  auto* obj = new T();
  obj->header.kind = kind;
  obj->header.refcnt = 1;
  xlang_perf_count_object_alloc(kind);
  return obj;
}

void release_string_block(StringObject* object) {
  const size_t alloc_size = object->alloc_size;
  object->~StringObject();
  memory::x3_thread_buckets().release(object, alloc_size);
}

StringObject* allocate_string_object(size_t size) {
  constexpr size_t kMaxStringPayload =
      static_cast<size_t>(std::numeric_limits<uint32_t>::max()) - sizeof(StringObject) - 1;
  if (size > kMaxStringPayload) {
    size = kMaxStringPayload;
  }
  const size_t total_size = sizeof(StringObject) + size + 1;
  void* block = memory::x3_thread_buckets().allocate(total_size);
  auto* obj = new (block) StringObject();
  obj->header.kind = ObjectKind::String;
  obj->header.refcnt = 1;
  obj->size = static_cast<uint32_t>(size);
  obj->alloc_size = static_cast<uint32_t>(total_size);
  string_object_mutable_data(*obj)[size] = '\0';
  xlang_perf_count_object_alloc(ObjectKind::String);
  return obj;
}

BytesObject* allocate_bytes_object(size_t size) {
  constexpr size_t kMaxBytesPayload =
      static_cast<size_t>(std::numeric_limits<uint32_t>::max()) - sizeof(BytesObject) - 1;
  if (size > kMaxBytesPayload) {
    size = kMaxBytesPayload;
  }
  const size_t total_size = sizeof(BytesObject) + size + 1;
  void* block = memory::x3_thread_buckets().allocate(total_size);
  auto* obj = new (block) BytesObject();
  obj->header.kind = ObjectKind::Bytes;
  obj->header.refcnt = 1;
  obj->size = static_cast<uint32_t>(size);
  obj->alloc_size = static_cast<uint32_t>(total_size);
  bytes_object_mutable_data(*obj)[size] = '\0';
  xlang_perf_count_object_alloc(ObjectKind::Bytes);
  return obj;
}

constexpr size_t align_up_size(size_t value, size_t alignment) {
  return (value + alignment - 1) & ~(alignment - 1);
}

constexpr size_t tuple_items_offset() {
  return align_up_size(sizeof(TupleObject), alignof(Value));
}

void release_tuple_block(TupleObject* object) {
  const size_t capacity = object->items.capacity();
  Value* item_storage = object->items.begin();
  const size_t alloc_size = object->alloc_size;
  object->items.clear();
  for (size_t i = 0; i < capacity; ++i) {
    item_storage[i].~Value();
  }
  object->~TupleObject();
  memory::x3_thread_buckets().release(object, alloc_size);
}

struct TupleObjectFreeLists {
  ~TupleObjectFreeLists() {
    for (auto& list : small) {
      for (auto* object : list) {
        release_tuple_block(object);
      }
    }
  }

  std::array<std::vector<TupleObject*>, 9> small;
};

thread_local TupleObjectFreeLists tuple_object_free_lists;

TupleObject* allocate_tuple_object(size_t capacity) {
  constexpr size_t kMaxTupleItems =
      (static_cast<size_t>(std::numeric_limits<uint32_t>::max()) - tuple_items_offset()) / sizeof(Value);
  if (capacity > kMaxTupleItems) {
    capacity = kMaxTupleItems;
  }
  if (capacity < tuple_object_free_lists.small.size()) {
    auto& list = tuple_object_free_lists.small[capacity];
    if (!list.empty()) {
      auto* obj = list.back();
      list.pop_back();
      obj->header.kind = ObjectKind::Tuple;
      obj->header.refcnt = 1;
      xlang_perf_count_object_alloc(ObjectKind::Tuple);
      return obj;
    }
  }
  const size_t total_size = tuple_items_offset() + capacity * sizeof(Value);
  void* block = memory::x3_thread_buckets().allocate(total_size);
  auto* obj = new (block) TupleObject();
  xlang_perf_count_object_alloc(ObjectKind::Tuple);
  obj->header.kind = ObjectKind::Tuple;
  obj->header.refcnt = 1;
  obj->alloc_size = static_cast<uint32_t>(total_size);
  auto* item_storage = reinterpret_cast<Value*>(static_cast<unsigned char*>(block) + tuple_items_offset());
  for (size_t i = 0; i < capacity; ++i) {
    new (item_storage + i) Value();
  }
  obj->items.bind(item_storage, static_cast<uint32_t>(capacity));
  return obj;
}

void recycle_tuple_object(TupleObject* object) {
  const uint32_t capacity = object->items.capacity();
  object->items.clear();
  if (capacity < tuple_object_free_lists.small.size()) {
    auto& list = tuple_object_free_lists.small[capacity];
    if (list.size() < 4096) {
      list.push_back(object);
      return;
    }
  }
  release_tuple_block(object);
}

void string_object_set_bytes(StringObject* object, const char* source, size_t size) {
  if (object->size != 0) {
    std::memcpy(string_object_mutable_data(*object), source, object->size);
  }
}

void recycle_string_object(StringObject* object) {
  release_string_block(object);
}

void recycle_bytes_object(BytesObject* object) {
  const size_t alloc_size = object->alloc_size;
  object->~BytesObject();
  memory::x3_thread_buckets().release(object, alloc_size);
}

bool is_number(const Value& value) {
  return value.tag == ValueTag::Int64 || value.tag == ValueTag::Double;
}

double as_double(const Value& value) {
  return value.tag == ValueTag::Int64 ? static_cast<double>(value.as.i64) : value.as.f64;
}

StringObject* as_string(Object* obj) {
  return reinterpret_cast<StringObject*>(obj);
}

BytesObject* as_bytes(Object* obj) {
  return reinterpret_cast<BytesObject*>(obj);
}

ByteArrayObject* as_bytearray(Object* obj) {
  return reinterpret_cast<ByteArrayObject*>(obj);
}

TupleObject* as_tuple(Object* obj) {
  return reinterpret_cast<TupleObject*>(obj);
}

std::string bytes_repr(std::string_view value) {
  std::string text = "b'";
  for (const unsigned char ch : value) {
    if (ch == '\\' || ch == '\'') {
      text.push_back('\\');
      text.push_back(static_cast<char>(ch));
    } else if (ch == '\n') {
      text += "\\n";
    } else if (ch == '\r') {
      text += "\\r";
    } else if (ch == '\t') {
      text += "\\t";
    } else if (ch >= 32 && ch < 127) {
      text.push_back(static_cast<char>(ch));
    } else {
      constexpr char hex[] = "0123456789abcdef";
      text += "\\x";
      text.push_back(hex[ch >> 4]);
      text.push_back(hex[ch & 0xf]);
    }
  }
  text.push_back('\'');
  return text;
}

FunctionObject* as_function(Object* obj) {
  return reinterpret_cast<FunctionObject*>(obj);
}

NativeFunctionObject* as_native_function(Object* obj) {
  return reinterpret_cast<NativeFunctionObject*>(obj);
}

CodeObject* as_code(Object* obj) {
  return reinterpret_cast<CodeObject*>(obj);
}

FrameObject* as_frame(Object* obj) {
  return reinterpret_cast<FrameObject*>(obj);
}

TracebackObject* as_traceback(Object* obj) {
  return reinterpret_cast<TracebackObject*>(obj);
}

FileObject* as_file(Object* obj) {
  return reinterpret_cast<FileObject*>(obj);
}

#if defined(XLANG3_EMBEDDED)
std::string format_i64(int64_t value) {
  char buffer[32];
  const int written = std::snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(value));
  if (written <= 0) {
    return "0";
  }
  const auto size = static_cast<size_t>(written);
  return std::string(buffer, size < sizeof(buffer) ? size : sizeof(buffer) - 1);
}

std::string format_f64(double value) {
  char buffer[48];
  const int written = std::snprintf(buffer, sizeof(buffer), "%.15g", value);
  if (written <= 0) {
    return "0";
  }
  const auto size = static_cast<size_t>(written);
  return std::string(buffer, size < sizeof(buffer) ? size : sizeof(buffer) - 1);
}
#endif

} // namespace

Value::Value(const Value& other) : tag(other.tag), flags(other.flags & ~kXlangValueBorrowedRefFlag), as(other.as) {
  retain(*this);
}

Value::Value(Value&& other) noexcept : tag(other.tag), flags(other.flags), as(other.as) {
  other.tag = ValueTag::Invalid;
  other.as.obj = nullptr;
}

Value& Value::operator=(const Value& other) {
  if (this == &other) {
    return *this;
  }
  release(*this);
  tag = other.tag;
  flags = other.flags & ~kXlangValueBorrowedRefFlag;
  as = other.as;
  retain(*this);
  return *this;
}

Value& Value::operator=(Value&& other) noexcept {
  if (this == &other) {
    return *this;
  }
  release(*this);
  tag = other.tag;
  flags = other.flags;
  as = other.as;
  other.tag = ValueTag::Invalid;
  other.as.obj = nullptr;
  return *this;
}

Value::~Value() {
  release(*this);
}

Value Value::string(std::string value) {
  return string_view(std::string_view(value.data(), value.size()));
}

Value Value::string_view(std::string_view value) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_string_object(value.size());
  string_object_set_bytes(obj, value.data(), value.size());
  v.as.obj = &obj->header;
  return v;
}

Value Value::string_uninitialized(size_t size) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_string_object(size);
  v.as.obj = &obj->header;
  return v;
}

Value Value::bytes(std::string value) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_bytes_object(value.size());
  if (!value.empty()) {
    std::memcpy(bytes_object_mutable_data(*obj), value.data(), value.size());
  }
  v.as.obj = &obj->header;
  return v;
}

Value Value::bytearray(std::string value) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_object<ByteArrayObject>(ObjectKind::ByteArray);
  obj->value = std::move(value);
  v.as.obj = &obj->header;
  return v;
}

Value Value::memoryview(Value owner, size_t offset, size_t size, bool readonly) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_object<MemoryViewObject>(ObjectKind::MemoryView);
  obj->owner = std::move(owner);
  obj->offset = offset;
  obj->size = size;
  obj->readonly = readonly;
  v.as.obj = &obj->header;
  return v;
}

Value Value::slice(Value start, Value stop, Value step) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_object<SliceObject>(ObjectKind::Slice);
  obj->start = std::move(start);
  obj->stop = std::move(stop);
  obj->step = std::move(step);
  v.as.obj = &obj->header;
  return v;
}

Value Value::tuple(std::vector<Value> items) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_tuple_object(items.size());
  for (auto& item : items) {
    obj->items.push_back_unchecked(std::move(item));
  }
  v.as.obj = &obj->header;
  return v;
}

Value Value::tuple_reserved(size_t capacity) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_tuple_object(capacity);
  v.as.obj = &obj->header;
  return v;
}

Value Value::cell(Value value) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_object<CellObject>(ObjectKind::Cell);
  obj->value = std::move(value);
  v.as.obj = &obj->header;
  return v;
}

Value Value::function(uint32_t function_id, std::vector<Value> closure) {
  return function(function_id, std::move(closure), Value::invalid());
}

Value Value::function(uint32_t function_id, std::vector<Value> closure, Value globals_module) {
  return function(function_id, std::move(closure), std::move(globals_module), nullptr);
}

Value Value::function(
    uint32_t function_id,
    std::vector<Value> closure,
    Value globals_module,
    std::shared_ptr<const ir::Module> module,
    std::vector<Value> defaults) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_object<FunctionObject>(ObjectKind::Function);
  obj->function_id = function_id;
  obj->closure = std::move(closure);
  obj->defaults = std::move(defaults);
  obj->globals_module = std::move(globals_module);
  obj->module = std::move(module);
  v.as.obj = &obj->header;
  return v;
}

Value Value::code(std::shared_ptr<const ir::Module> module, uint32_t function_id) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_object<CodeObject>(ObjectKind::Code);
  obj->module = std::move(module);
  obj->function_id = function_id;
  v.as.obj = &obj->header;
  return v;
}

Value Value::frame(std::shared_ptr<const ir::Module> module, uint32_t function_id, Value globals_module) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_object<FrameObject>(ObjectKind::Frame);
  obj->module = std::move(module);
  obj->function_id = function_id;
  obj->globals_module = std::move(globals_module);
  v.as.obj = &obj->header;
  return v;
}

Value Value::traceback(Value frame, Value next, int64_t line) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_object<TracebackObject>(ObjectKind::Traceback);
  obj->frame = std::move(frame);
  obj->next = std::move(next);
  obj->line = line;
  v.as.obj = &obj->header;
  return v;
}

Value Value::native_function(
    uint32_t native_id,
    std::string name,
    NativeFunctionCallback callback,
    void* user_data,
    void (*user_data_cleanup)(void*),
    NativeFastCallCallback fast_callback,
    bool fast_releases_vm_lock,
    NativeKeywordFunctionCallback keyword_callback) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_object<NativeFunctionObject>(ObjectKind::NativeFunction);
  obj->native_id = native_id;
  obj->name = std::move(name);
  obj->callback = callback;
  obj->keyword_callback = keyword_callback;
  obj->fast_callback = fast_callback;
  obj->fast_releases_vm_lock = fast_releases_vm_lock;
  obj->user_data = user_data;
  obj->user_data_cleanup = user_data_cleanup;
  v.as.obj = &obj->header;
  return v;
}

Value Value::property(Value fget, Value fset, Value fdel, Value doc) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_object<PropertyObject>(ObjectKind::Property);
  obj->fget = std::move(fget);
  obj->fset = std::move(fset);
  obj->fdel = std::move(fdel);
  obj->doc = std::move(doc);
  v.as.obj = &obj->header;
  return v;
}

Value Value::file(FileSystem* fs, std::string path, std::string mode, std::string buffer, bool writable) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_object<FileObject>(ObjectKind::File);
  obj->fs = fs;
  obj->path = std::move(path);
  obj->mode = std::move(mode);
  obj->buffer = std::move(buffer);
  obj->writable = writable;
  v.as.obj = &obj->header;
  return v;
}

void retain(const Value& value) {
  if (value.tag == ValueTag::Object && value.as.obj != nullptr) {
    if ((value.flags & kXlangValueBorrowedRefFlag) != 0) {
      return;
    }
    xlang_perf_count_value_incref(value.as.obj->kind);
    value.as.obj->refcnt.fetch_add(1, std::memory_order_relaxed);
  }
}

void release(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr) {
    return;
  }
  if ((value.flags & kXlangValueBorrowedRefFlag) != 0) {
    return;
  }
  xlang_perf_count_value_decref(value.as.obj->kind);
  if (value.as.obj->refcnt.fetch_sub(1, std::memory_order_acq_rel) != 1) {
    return;
  }
  xlang_perf_count_object_final_release(value.as.obj->kind);
  switch (value.as.obj->kind) {
    case ObjectKind::String:
      recycle_string_object(as_string(value.as.obj));
      break;
    case ObjectKind::Bytes:
      recycle_bytes_object(as_bytes(value.as.obj));
      break;
    case ObjectKind::ByteArray:
      delete as_bytearray(value.as.obj);
      break;
    case ObjectKind::MemoryView:
      delete value_as_memoryview(value);
      break;
    case ObjectKind::Slice:
      delete value_as_slice(value);
      break;
    case ObjectKind::Tuple:
      recycle_tuple_object(as_tuple(value.as.obj));
      break;
    case ObjectKind::Dict:
    case ObjectKind::DictKeysView:
    case ObjectKind::DictValuesView:
    case ObjectKind::DictItemsView:
    case ObjectKind::DictIterator:
      mapping_release_object(value.as.obj);
      break;
    case ObjectKind::Set:
    case ObjectKind::SetIterator:
      set_release_object(value.as.obj);
      break;
    case ObjectKind::Module:
      module_release_object(value.as.obj);
      break;
    case ObjectKind::List:
    case ObjectKind::Range:
    case ObjectKind::RangeIterator:
    case ObjectKind::SequenceIterator:
      sequence_release_object(value.as.obj);
      break;
    case ObjectKind::EnumerateIterator:
    case ObjectKind::ZipIterator:
    case ObjectKind::MapIterator:
    case ObjectKind::FilterIterator:
      functional_iterator_release_object(value.as.obj);
      break;
    case ObjectKind::Generator:
      generator_release_object(value.as.obj);
      break;
    case ObjectKind::Cell:
      delete reinterpret_cast<CellObject*>(value.as.obj);
      break;
    case ObjectKind::Function:
      delete as_function(value.as.obj);
      break;
    case ObjectKind::NativeFunction:
      if (as_native_function(value.as.obj)->user_data_cleanup != nullptr) {
        as_native_function(value.as.obj)->user_data_cleanup(as_native_function(value.as.obj)->user_data);
      }
      delete as_native_function(value.as.obj);
      break;
    case ObjectKind::Code:
      delete as_code(value.as.obj);
      break;
    case ObjectKind::Frame:
      delete as_frame(value.as.obj);
      break;
    case ObjectKind::Traceback:
      delete as_traceback(value.as.obj);
      break;
    case ObjectKind::Class:
    case ObjectKind::Instance:
    case ObjectKind::BoundMethod:
      object_model_release_object(value.as.obj);
      break;
    case ObjectKind::Property:
      delete value_as_property(value);
      break;
    case ObjectKind::File:
      delete as_file(value.as.obj);
      break;
  }
}

std::string value_to_string(const Value& value) {
  switch (value.tag) {
    case ValueTag::Invalid:
      return "<invalid>";
    case ValueTag::None:
      return "None";
    case ValueTag::Bool:
      return value.as.b ? "True" : "False";
    case ValueTag::Int64:
#if defined(XLANG3_EMBEDDED)
      return format_i64(value.as.i64);
#else
      return std::to_string(value.as.i64);
#endif
    case ValueTag::Double: {
#if defined(XLANG3_EMBEDDED)
      return format_f64(value.as.f64);
#else
      std::ostringstream os;
      os << value.as.f64;
      return os.str();
#endif
    }
    case ValueTag::Object:
      if (value.as.obj != nullptr && value.as.obj->kind == ObjectKind::String) {
        return string_object_to_string(*as_string(value.as.obj));
      }
      if (value.as.obj != nullptr && value.as.obj->kind == ObjectKind::Bytes) {
        return bytes_repr(bytes_object_view(*as_bytes(value.as.obj)));
      }
      if (value.as.obj != nullptr && value.as.obj->kind == ObjectKind::ByteArray) {
        return "bytearray(" + bytes_repr(as_bytearray(value.as.obj)->value) + ")";
      }
      if (value.as.obj != nullptr && value.as.obj->kind == ObjectKind::MemoryView) {
        return "<memoryview>";
      }
      if (value.as.obj != nullptr && value.as.obj->kind == ObjectKind::Slice) {
        return "slice(" + value_to_string(value_as_slice(value)->start) + ", " +
               value_to_string(value_as_slice(value)->stop) + ", " +
               value_to_string(value_as_slice(value)->step) + ")";
      }
      if (value.as.obj != nullptr && value.as.obj->kind == ObjectKind::Tuple) {
        const auto& items = as_tuple(value.as.obj)->items;
        std::string text = "(";
        for (size_t i = 0; i < items.size(); ++i) {
          if (i != 0) {
            text += ", ";
          }
          text += value_to_string(items[i]);
        }
        if (items.size() == 1) {
          text += ",";
        }
        text += ")";
        return text;
      }
      if (value.as.obj != nullptr &&
          (value.as.obj->kind == ObjectKind::List ||
           value.as.obj->kind == ObjectKind::Range ||
           value.as.obj->kind == ObjectKind::RangeIterator ||
           value.as.obj->kind == ObjectKind::SequenceIterator)) {
        return sequence_to_string(value);
      }
      if (value_is_functional_iterator(value)) {
        return functional_iterator_to_string(value);
      }
      if (value.as.obj != nullptr && value.as.obj->kind == ObjectKind::Generator) {
        return generator_to_string(value);
      }
      if (value.as.obj != nullptr &&
          (value.as.obj->kind == ObjectKind::Dict ||
           value.as.obj->kind == ObjectKind::DictKeysView ||
           value.as.obj->kind == ObjectKind::DictValuesView ||
           value.as.obj->kind == ObjectKind::DictItemsView ||
           value.as.obj->kind == ObjectKind::DictIterator)) {
        return mapping_to_string(value);
      }
      if (value.as.obj != nullptr &&
          (value.as.obj->kind == ObjectKind::Set ||
           value.as.obj->kind == ObjectKind::SetIterator)) {
        return set_to_string(value);
      }
      if (value.as.obj != nullptr && value.as.obj->kind == ObjectKind::Module) {
        return module_to_string(value);
      }
      if (value.as.obj != nullptr && value.as.obj->kind == ObjectKind::Cell) {
        return "<cell>";
      }
      if (value.as.obj != nullptr && value.as.obj->kind == ObjectKind::Function) {
        return "<function>";
      }
      if (value.as.obj != nullptr && value.as.obj->kind == ObjectKind::NativeFunction) {
        return "<built-in function " + as_native_function(value.as.obj)->name + ">";
      }
      if (value.as.obj != nullptr && value.as.obj->kind == ObjectKind::Code) {
        return "<code object>";
      }
      if (value.as.obj != nullptr && value.as.obj->kind == ObjectKind::Frame) {
        return "<frame object>";
      }
      if (value.as.obj != nullptr && value.as.obj->kind == ObjectKind::Traceback) {
        return "<traceback object>";
      }
      if (value.as.obj != nullptr && value.as.obj->kind == ObjectKind::Property) {
        return "<property object>";
      }
      if (value.as.obj != nullptr &&
          (value.as.obj->kind == ObjectKind::Class ||
           value.as.obj->kind == ObjectKind::Instance ||
           value.as.obj->kind == ObjectKind::BoundMethod)) {
        return object_model_to_string(value);
      }
      if (value.as.obj != nullptr && value.as.obj->kind == ObjectKind::File) {
        return "<file '" + as_file(value.as.obj)->path + "'>";
      }
      return "<object>";
  }
  return "<unknown>";
}

bool value_truthy(const Value& value) {
  switch (value.tag) {
    case ValueTag::Invalid:
    case ValueTag::None:
      return false;
    case ValueTag::Bool:
      return value.as.b;
    case ValueTag::Int64:
      return value.as.i64 != 0;
    case ValueTag::Double:
      return value.as.f64 != 0.0;
    case ValueTag::Object:
      if (value.as.obj != nullptr && value.as.obj->kind == ObjectKind::String) {
        return as_string(value.as.obj)->size != 0;
      }
      if (value.as.obj != nullptr && value.as.obj->kind == ObjectKind::Bytes) {
        return as_bytes(value.as.obj)->size != 0;
      }
      if (value.as.obj != nullptr && value.as.obj->kind == ObjectKind::ByteArray) {
        return !as_bytearray(value.as.obj)->value.empty();
      }
      if (auto* view = value_as_memoryview(value)) {
        return view->size != 0;
      }
      if (value.as.obj != nullptr && value.as.obj->kind == ObjectKind::Tuple) {
        return !as_tuple(value.as.obj)->items.empty();
      }
      if (value.as.obj != nullptr &&
          (value.as.obj->kind == ObjectKind::List ||
           value.as.obj->kind == ObjectKind::Range ||
           value.as.obj->kind == ObjectKind::RangeIterator ||
           value.as.obj->kind == ObjectKind::SequenceIterator)) {
        return sequence_truthy(value);
      }
      if (value_is_functional_iterator(value)) {
        return true;
      }
      if (value.as.obj != nullptr && value.as.obj->kind == ObjectKind::Generator) {
        return generator_truthy(value);
      }
      if (value.as.obj != nullptr &&
          (value.as.obj->kind == ObjectKind::Dict ||
           value.as.obj->kind == ObjectKind::DictKeysView ||
           value.as.obj->kind == ObjectKind::DictValuesView ||
           value.as.obj->kind == ObjectKind::DictItemsView ||
           value.as.obj->kind == ObjectKind::DictIterator)) {
        return mapping_truthy(value);
      }
      if (value.as.obj != nullptr &&
          (value.as.obj->kind == ObjectKind::Set ||
           value.as.obj->kind == ObjectKind::SetIterator)) {
        return set_truthy(value);
      }
      return true;
  }
  return false;
}

bool value_add(const Value& lhs, const Value& rhs, Value& out, std::string& error) {
  if (lhs.tag == ValueTag::Int64 && rhs.tag == ValueTag::Int64) {
    value_set_int64(out, lhs.as.i64 + rhs.as.i64);
    return true;
  }
  if (is_number(lhs) && is_number(rhs)) {
    value_set_number(out, as_double(lhs) + as_double(rhs));
    return true;
  }
  if (lhs.tag == ValueTag::Object && rhs.tag == ValueTag::Object &&
      lhs.as.obj != nullptr && rhs.as.obj != nullptr &&
      lhs.as.obj->kind == ObjectKind::String && rhs.as.obj->kind == ObjectKind::String) {
    const auto left = string_object_view(*as_string(lhs.as.obj));
    const auto right = string_object_view(*as_string(rhs.as.obj));
    out = Value::string_uninitialized(left.size() + right.size());
    auto* string = value_as_string(out);
    char* target = string_object_mutable_data(*string);
    if (!left.empty()) {
      std::memcpy(target, left.data(), left.size());
    }
    if (!right.empty()) {
      std::memcpy(target + left.size(), right.data(), right.size());
    }
    return true;
  }
  if (lhs.tag == ValueTag::Object && rhs.tag == ValueTag::Object &&
      lhs.as.obj != nullptr && rhs.as.obj != nullptr &&
      lhs.as.obj->kind == ObjectKind::Bytes && rhs.as.obj->kind == ObjectKind::Bytes) {
    const auto left = bytes_object_view(*as_bytes(lhs.as.obj));
    const auto right = bytes_object_view(*as_bytes(rhs.as.obj));
    std::string bytes;
    bytes.resize(left.size() + right.size());
    if (!left.empty()) {
      std::memcpy(bytes.data(), left.data(), left.size());
    }
    if (!right.empty()) {
      std::memcpy(bytes.data() + left.size(), right.data(), right.size());
    }
    out = Value::bytes(std::move(bytes));
    return true;
  }
  error = "unsupported operands for +";
  return false;
}

bool value_sub(const Value& lhs, const Value& rhs, Value& out, std::string& error) {
  if (lhs.tag == ValueTag::Int64 && rhs.tag == ValueTag::Int64) {
    value_set_int64(out, lhs.as.i64 - rhs.as.i64);
    return true;
  }
  if (is_number(lhs) && is_number(rhs)) {
    value_set_number(out, as_double(lhs) - as_double(rhs));
    return true;
  }
  error = "unsupported operands for -";
  return false;
}

bool value_mul(const Value& lhs, const Value& rhs, Value& out, std::string& error) {
  if (lhs.tag == ValueTag::Int64 && rhs.tag == ValueTag::Int64) {
    value_set_int64(out, lhs.as.i64 * rhs.as.i64);
    return true;
  }
  if (is_number(lhs) && is_number(rhs)) {
    value_set_number(out, as_double(lhs) * as_double(rhs));
    return true;
  }
  error = "unsupported operands for *";
  return false;
}

bool value_div(const Value& lhs, const Value& rhs, Value& out, std::string& error) {
  if (!is_number(lhs) || !is_number(rhs)) {
    error = "unsupported operands for /";
    return false;
  }
  const double divisor = as_double(rhs);
  if (divisor == 0.0) {
    error = "division by zero";
    return false;
  }
  value_set_number(out, as_double(lhs) / divisor);
  return true;
}

bool value_floor_div(const Value& lhs, const Value& rhs, Value& out, std::string& error) {
  if (lhs.tag == ValueTag::Int64 && rhs.tag == ValueTag::Int64) {
    if (rhs.as.i64 == 0) {
      error = "integer division by zero";
      return false;
    }
    int64_t q = lhs.as.i64 / rhs.as.i64;
    const int64_t r = lhs.as.i64 % rhs.as.i64;
    if (r != 0 && ((r < 0) != (rhs.as.i64 < 0))) {
      --q;
    }
    value_set_int64(out, q);
    return true;
  }
  if (!is_number(lhs) || !is_number(rhs)) {
    error = "unsupported operands for //";
    return false;
  }
  const double divisor = as_double(rhs);
  if (divisor == 0.0) {
    error = "float floor division by zero";
    return false;
  }
  value_set_number(out, std::floor(as_double(lhs) / divisor));
  return true;
}

bool value_mod(const Value& lhs, const Value& rhs, Value& out, std::string& error) {
  if (lhs.tag == ValueTag::Int64 && rhs.tag == ValueTag::Int64) {
    if (rhs.as.i64 == 0) {
      error = "integer modulo by zero";
      return false;
    }
    int64_t result = lhs.as.i64 % rhs.as.i64;
    if (result != 0 && ((result < 0) != (rhs.as.i64 < 0))) {
      result += rhs.as.i64;
    }
    value_set_int64(out, result);
    return true;
  }
  if (!is_number(lhs) || !is_number(rhs)) {
    error = "unsupported operands for %";
    return false;
  }
  const double divisor = as_double(rhs);
  if (divisor == 0.0) {
    error = "float modulo by zero";
    return false;
  }
  double result = std::fmod(as_double(lhs), divisor);
  if (result != 0.0 && ((result < 0.0) != (divisor < 0.0))) {
    result += divisor;
  }
  value_set_number(out, result);
  return true;
}

bool value_pow(const Value& lhs, const Value& rhs, Value& out, std::string& error) {
  if (!is_number(lhs) || !is_number(rhs)) {
    error = "unsupported operands for **";
    return false;
  }
  if (lhs.tag == ValueTag::Int64 && rhs.tag == ValueTag::Int64 && rhs.as.i64 >= 0) {
    int64_t result = 1;
    int64_t base = lhs.as.i64;
    uint64_t exponent = static_cast<uint64_t>(rhs.as.i64);
    while (exponent != 0) {
      if ((exponent & 1u) != 0) {
        result *= base;
      }
      exponent >>= 1u;
      if (exponent != 0) {
        base *= base;
      }
    }
    value_set_int64(out, result);
    return true;
  }
  value_set_number(out, std::pow(as_double(lhs), as_double(rhs)));
  return true;
}

bool value_bit_and(const Value& lhs, const Value& rhs, Value& out, std::string& error) {
  if (lhs.tag != ValueTag::Int64 || rhs.tag != ValueTag::Int64) {
    error = "unsupported operands for &";
    return false;
  }
  value_set_int64(out, lhs.as.i64 & rhs.as.i64);
  return true;
}

bool value_bit_or(const Value& lhs, const Value& rhs, Value& out, std::string& error) {
  if (lhs.tag != ValueTag::Int64 || rhs.tag != ValueTag::Int64) {
    error = "unsupported operands for |";
    return false;
  }
  value_set_int64(out, lhs.as.i64 | rhs.as.i64);
  return true;
}

bool value_bit_xor(const Value& lhs, const Value& rhs, Value& out, std::string& error) {
  if (lhs.tag != ValueTag::Int64 || rhs.tag != ValueTag::Int64) {
    error = "unsupported operands for ^";
    return false;
  }
  value_set_int64(out, lhs.as.i64 ^ rhs.as.i64);
  return true;
}

bool value_shift_left(const Value& lhs, const Value& rhs, Value& out, std::string& error) {
  if (lhs.tag != ValueTag::Int64 || rhs.tag != ValueTag::Int64) {
    error = "unsupported operands for <<";
    return false;
  }
  if (rhs.as.i64 < 0) {
    error = "negative shift count";
    return false;
  }
  if (rhs.as.i64 >= 63) {
    error = "shift count too large for int64";
    return false;
  }
  value_set_int64(out, lhs.as.i64 << rhs.as.i64);
  return true;
}

bool value_shift_right(const Value& lhs, const Value& rhs, Value& out, std::string& error) {
  if (lhs.tag != ValueTag::Int64 || rhs.tag != ValueTag::Int64) {
    error = "unsupported operands for >>";
    return false;
  }
  if (rhs.as.i64 < 0) {
    error = "negative shift count";
    return false;
  }
  if (rhs.as.i64 >= 63) {
    value_set_int64(out, lhs.as.i64 < 0 ? -1 : 0);
    return true;
  }
  value_set_int64(out, lhs.as.i64 >> rhs.as.i64);
  return true;
}

bool value_invert(const Value& value, Value& out, std::string& error) {
  if (value.tag != ValueTag::Int64) {
    error = "unsupported operand for unary ~";
    return false;
  }
  value_set_int64(out, ~value.as.i64);
  return true;
}

bool value_compare(const std::string& op, const Value& lhs, const Value& rhs, Value& out, std::string& error) {
  bool result = false;
  if (is_number(lhs) && is_number(rhs)) {
    const double a = as_double(lhs);
    const double b = as_double(rhs);
    if (op == "==") result = a == b;
    else if (op == "!=") result = a != b;
    else if (op == "<") result = a < b;
    else if (op == "<=") result = a <= b;
    else if (op == ">") result = a > b;
    else if (op == ">=") result = a >= b;
    else {
      error = "unknown comparison operator";
      return false;
    }
    value_set_bool(out, result);
    return true;
  }
  if (op == "==" || op == "!=") {
    if (lhs.tag == ValueTag::Object && rhs.tag == ValueTag::Object &&
        lhs.as.obj != nullptr && rhs.as.obj != nullptr &&
        lhs.as.obj->kind == ObjectKind::Bytes && rhs.as.obj->kind == ObjectKind::Bytes) {
      result = bytes_object_view(*as_bytes(lhs.as.obj)) == bytes_object_view(*as_bytes(rhs.as.obj));
    } else {
      result = value_to_string(lhs) == value_to_string(rhs);
    }
    value_set_bool(out, op == "==" ? result : !result);
    return true;
  }
  error = "unsupported comparison";
  return false;
}

bool value_is(const Value& lhs, const Value& rhs) {
  if (lhs.tag != rhs.tag) {
    return false;
  }
  switch (lhs.tag) {
    case ValueTag::Invalid:
    case ValueTag::None:
      return true;
    case ValueTag::Bool:
      return lhs.as.b == rhs.as.b;
    case ValueTag::Int64:
      return lhs.as.i64 == rhs.as.i64;
    case ValueTag::Double:
      return lhs.as.f64 == rhs.as.f64;
    case ValueTag::Object:
      return lhs.as.obj == rhs.as.obj;
  }
  return false;
}

bool value_contains(const Value& container, const Value& item, bool& out, std::string& error) {
  out = false;
  if (auto* list = value_as_list(container)) {
    for (const auto& candidate : list->items) {
      if (value_key_equal(candidate, item)) {
        out = true;
        return true;
      }
    }
    return true;
  }
  if (container.tag == ValueTag::Object && container.as.obj != nullptr && container.as.obj->kind == ObjectKind::Tuple) {
    auto* tuple = reinterpret_cast<TupleObject*>(container.as.obj);
    for (const auto& candidate : tuple->items) {
      if (value_key_equal(candidate, item)) {
        out = true;
        return true;
      }
    }
    return true;
  }
  if (container.tag == ValueTag::Object && container.as.obj != nullptr && container.as.obj->kind == ObjectKind::String) {
    auto* haystack = reinterpret_cast<StringObject*>(container.as.obj);
    auto* needle = value_as_string(item);
    if (needle == nullptr) {
      error = "'in <string>' requires string as left operand";
      return false;
    }
    out = string_object_view(*haystack).find(string_object_view(*needle)) != std::string_view::npos;
    return true;
  }
  if (container.tag == ValueTag::Object && container.as.obj != nullptr && container.as.obj->kind == ObjectKind::Bytes) {
    auto* haystack = as_bytes(container.as.obj);
    if (item.tag == ValueTag::Int64) {
      if (item.as.i64 < 0 || item.as.i64 > 255) {
        out = false;
        return true;
      }
      out = bytes_object_view(*haystack).find(static_cast<char>(item.as.i64)) != std::string_view::npos;
      return true;
    }
    if (item.tag == ValueTag::Object && item.as.obj != nullptr && item.as.obj->kind == ObjectKind::Bytes) {
      out = bytes_object_view(*haystack).find(bytes_object_view(*as_bytes(item.as.obj))) != std::string_view::npos;
      return true;
    }
    error = "'in <bytes>' requires int or bytes as left operand";
    return false;
  }
  if (auto* dict = value_as_dict(container)) {
    for (const auto& entry : dict->entries) {
      if (value_key_equal(entry.first, item)) {
        out = true;
        return true;
      }
    }
    return true;
  }
  if (value_as_dict_view(container) != nullptr) {
    return mapping_contains(container, item, out, error);
  }
  if (auto* set = value_as_set(container)) {
    for (const auto& candidate : set->items) {
      if (value_key_equal(candidate, item)) {
        out = true;
        return true;
      }
    }
    return true;
  }
  if (auto* range = value_as_range(container)) {
    if (item.tag != ValueTag::Int64 || range->step == 0) {
      return true;
    }
    const int64_t value = item.as.i64;
    const bool in_bounds = range->step > 0 ? (value >= range->start && value < range->stop)
                                           : (value <= range->start && value > range->stop);
    out = in_bounds && ((value - range->start) % range->step == 0);
    return true;
  }
  error = "object is not a container";
  return false;
}

} // namespace xlang3
