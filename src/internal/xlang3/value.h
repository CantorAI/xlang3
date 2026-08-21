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

#include "xlang3/compiler.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace xlang3 {

namespace ir {
struct Module;
}

class Runtime;
class FileSystem;
struct Value;

enum class ValueTag : uint32_t {
  Invalid = 0,
  None,
  Bool,
  Int64,
  Double,
  Object,
};

enum class ObjectKind : uint32_t {
  String = 1,
  Bytes,
  ByteArray,
  MemoryView,
  Slice,
  Tuple,
  List,
  Dict,
  Set,
  DictKeysView,
  DictValuesView,
  DictItemsView,
  DictIterator,
  SetIterator,
  Range,
  RangeIterator,
  SequenceIterator,
  EnumerateIterator,
  ZipIterator,
  MapIterator,
  FilterIterator,
  Generator,
  Module,
  Cell,
  Function,
  NativeFunction,
  Class,
  Instance,
  BoundMethod,
  Property,
  Code,
  Frame,
  Traceback,
  File,
};

struct Object {
  ObjectKind kind;
  std::atomic_uint32_t refcnt;
};

static constexpr uint32_t kXlangValueBorrowedRefFlag = 0x40000000u;

using NativeFunctionCallback = bool (*)(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data);

struct NativeKeywordArg {
  const char* name = nullptr;
  const Value* value = nullptr;
};

using NativeKeywordFunctionCallback = bool (*)(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data);

using NativeFastCallCallback = bool (*)(
    Runtime& runtime,
    const Value* leading,
    uint32_t leading_count,
    const Value* registers,
    const uint32_t* register_args,
    uint32_t register_arg_count,
    Value& out,
    std::string& error,
    void* user_data);

struct NativeFunctionObject {
  Object header;
  uint32_t native_id = 0;
  std::string name;
  NativeFunctionCallback callback = nullptr;
  NativeKeywordFunctionCallback keyword_callback = nullptr;
  NativeFastCallCallback fast_callback = nullptr;
  bool fast_releases_vm_lock = false;
  void* user_data = nullptr;
  void (*user_data_cleanup)(void*) = nullptr;
};

struct StringObject {
  Object header;
  uint32_t size = 0;
  uint32_t alloc_size = 0;
  // Immutable string bytes follow this object in the same allocation block.
};

struct BytesObject {
  Object header;
  uint32_t size = 0;
  uint32_t alloc_size = 0;
  // Immutable bytes follow this object in the same allocation block.
};

struct ByteArrayObject {
  Object header;
  std::string value;
};

struct Value {
  ValueTag tag = ValueTag::Invalid;
  uint32_t flags = 0;
  union {
    bool b;
    int64_t i64;
    double f64;
    Object* obj;
  } as{};

  Value() = default;
  Value(const Value& other);
  Value(Value&& other) noexcept;
  Value& operator=(const Value& other);
  Value& operator=(Value&& other) noexcept;
  ~Value();

  static Value invalid();
  static Value none();
  static Value boolean(bool value);
  static Value int64(int64_t value);
  static Value number(double value);
  static Value string(std::string value);
  static Value string_view(std::string_view value);
  static Value string_uninitialized(size_t size);
  static Value bytes(std::string value);
  static Value bytearray(std::string value);
  static Value memoryview(Value owner, size_t offset, size_t size, bool readonly);
  static Value slice(Value start, Value stop, Value step);
  static Value tuple(std::vector<Value> items);
  static Value tuple_reserved(size_t capacity);
  static Value list(std::vector<Value> items);
  static Value list_reserved(size_t capacity);
  static Value dict(std::vector<std::pair<Value, Value>> entries);
  static Value dict_reserved(size_t capacity);
  static Value set(std::vector<Value> items);
  static Value range(int64_t start, int64_t stop, int64_t step);
  static Value range_iterator(int64_t current, int64_t stop, int64_t step);
  static Value sequence_iterator(Value source, uint64_t index);
  static Value generator(Runtime* runtime, Value function, std::vector<Value> args);
  static Value module(std::string name);
  static Value cell(Value value);
  static Value function(uint32_t function_id, std::vector<Value> closure);
  static Value function(uint32_t function_id, std::vector<Value> closure, Value globals_module);
  static Value function(
      uint32_t function_id,
      std::vector<Value> closure,
      Value globals_module,
      std::shared_ptr<const ir::Module> module,
      std::vector<Value> defaults = {});
  static Value code(std::shared_ptr<const ir::Module> module, uint32_t function_id, std::string mode = "exec");
  static Value frame(std::shared_ptr<const ir::Module> module, uint32_t function_id, Value globals_module);
  static Value traceback(Value frame, Value next, int64_t line);
  static Value native_function(
      uint32_t native_id,
      std::string name,
      NativeFunctionCallback callback,
      void* user_data = nullptr,
      void (*user_data_cleanup)(void*) = nullptr,
      NativeFastCallCallback fast_callback = nullptr,
      bool fast_releases_vm_lock = false,
      NativeKeywordFunctionCallback keyword_callback = nullptr);
  static Value file(FileSystem* fs, std::string path, std::string mode, std::string buffer, bool writable);
  static Value class_object(
      std::string name,
      std::vector<std::pair<std::string, Value>> attrs,
      Value base = Value::invalid(),
      std::vector<std::string> instance_slots = {});
  static Value instance(Value klass);
  static Value bound_method(Value self, Value function);
  static Value property(Value fget, Value fset, Value fdel, Value doc);
};

XLANG3_HOT_INLINE Value Value::invalid() {
  return {};
}

XLANG3_HOT_INLINE Value Value::none() {
  Value v;
  v.tag = ValueTag::None;
  return v;
}

XLANG3_HOT_INLINE Value Value::boolean(bool value) {
  Value v;
  v.tag = ValueTag::Bool;
  v.as.b = value;
  return v;
}

XLANG3_HOT_INLINE Value Value::int64(int64_t value) {
  Value v;
  v.tag = ValueTag::Int64;
  v.as.i64 = value;
  return v;
}

XLANG3_HOT_INLINE Value Value::number(double value) {
  Value v;
  v.tag = ValueTag::Double;
  v.as.f64 = value;
  return v;
}

struct SliceObject {
  Object header;
  Value start;
  Value stop;
  Value step;
};

XLANG3_HOT_INLINE void value_assign_fast(Value& out, const Value& value);
XLANG3_HOT_INLINE void value_move_assign_fast(Value& out, Value& value);
XLANG3_HOT_INLINE void value_set_invalid(Value& out);

class TupleItems {
public:
  using iterator = Value*;
  using const_iterator = const Value*;

  TupleItems() = default;

  size_t size() const { return size_; }
  bool empty() const { return size_ == 0; }
  iterator begin() { return data_; }
  iterator end() { return data_ + size_; }
  const_iterator begin() const { return data_; }
  const_iterator end() const { return data_ + size_; }
  const_iterator cbegin() const { return data_; }
  const_iterator cend() const { return data_ + size_; }

  Value& operator[](size_t index) { return data_[index]; }
  const Value& operator[](size_t index) const { return data_[index]; }

  void bind(Value* data, uint32_t capacity) {
    data_ = data;
    size_ = 0;
    capacity_ = capacity;
  }

  void push_back(const Value& value) {
    if (size_ >= capacity_) {
      return;
    }
    value_assign_fast(data_[size_], value);
    ++size_;
  }

  void push_back(Value&& value) {
    if (size_ >= capacity_) {
      return;
    }
    value_move_assign_fast(data_[size_], value);
    ++size_;
  }

  XLANG3_HOT_INLINE void push_back_unchecked(const Value& value) {
    value_assign_fast(data_[size_], value);
    ++size_;
  }

  XLANG3_HOT_INLINE void push_back_unchecked(Value&& value) {
    value_move_assign_fast(data_[size_], value);
    ++size_;
  }

  void clear() {
    for (uint32_t i = 0; i < size_; ++i) {
      value_set_invalid(data_[i]);
    }
    size_ = 0;
  }

  TupleItems& operator=(std::vector<Value> values) {
    clear();
    for (auto& value : values) {
      push_back(std::move(value));
    }
    return *this;
  }

  operator std::vector<Value>() const {
    std::vector<Value> values;
    values.reserve(size_);
    for (uint32_t i = 0; i < size_; ++i) {
      values.push_back(data_[i]);
    }
    return values;
  }

  uint32_t capacity() const { return capacity_; }

private:
  Value* data_ = nullptr;
  uint32_t size_ = 0;
  uint32_t capacity_ = 0;
};

struct TupleObject {
  Object header;
  uint32_t alloc_size = 0;
  TupleItems items;
};

struct CellObject {
  Object header;
  Value value;
};

struct FunctionObject {
  Object header;
  uint32_t function_id = 0;
  std::vector<Value> closure;
  std::vector<Value> defaults;
  Value annotations;
  Value globals_module;
  std::shared_ptr<const ir::Module> module;
};

struct CodeObject {
  Object header;
  std::shared_ptr<const ir::Module> module;
  uint32_t function_id = 0;
  std::string mode;
};

struct FrameObject {
  Object header;
  std::shared_ptr<const ir::Module> module;
  uint32_t function_id = 0;
  Value globals_module;
};

struct MemoryViewObject {
  Object header;
  Value owner;
  size_t offset = 0;
  size_t size = 0;
  bool readonly = true;
};

struct PropertyObject {
  Object header;
  Value fget;
  Value fset;
  Value fdel;
  Value doc;
};

struct TracebackObject {
  Object header;
  Value frame;
  Value next;
  int64_t line = 0;
};

XLANG3_HOT_INLINE StringObject* value_as_string(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::String) {
    return nullptr;
  }
  return reinterpret_cast<StringObject*>(value.as.obj);
}

XLANG3_HOT_INLINE std::string_view string_object_view(const StringObject& value) {
  return std::string_view(reinterpret_cast<const char*>(&value + 1), value.size);
}

XLANG3_HOT_INLINE const char* string_object_c_str(const StringObject& value) {
  return reinterpret_cast<const char*>(&value + 1);
}

XLANG3_HOT_INLINE char* string_object_mutable_data(StringObject& value) {
  return reinterpret_cast<char*>(&value + 1);
}

inline std::string string_object_to_string(const StringObject& value) {
  return std::string(string_object_view(value));
}

XLANG3_HOT_INLINE BytesObject* value_as_bytes(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::Bytes) {
    return nullptr;
  }
  return reinterpret_cast<BytesObject*>(value.as.obj);
}

XLANG3_HOT_INLINE std::string_view bytes_object_view(const BytesObject& value) {
  return std::string_view(reinterpret_cast<const char*>(&value + 1), value.size);
}

XLANG3_HOT_INLINE char* bytes_object_mutable_data(BytesObject& value) {
  return reinterpret_cast<char*>(&value + 1);
}

inline std::string bytes_object_to_string(const BytesObject& value) {
  return std::string(bytes_object_view(value));
}

XLANG3_HOT_INLINE SliceObject* value_as_slice(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::Slice) {
    return nullptr;
  }
  return reinterpret_cast<SliceObject*>(value.as.obj);
}

XLANG3_HOT_INLINE TupleObject* value_as_tuple(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::Tuple) {
    return nullptr;
  }
  return reinterpret_cast<TupleObject*>(value.as.obj);
}

XLANG3_HOT_INLINE ByteArrayObject* value_as_bytearray(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::ByteArray) {
    return nullptr;
  }
  return reinterpret_cast<ByteArrayObject*>(value.as.obj);
}

XLANG3_HOT_INLINE MemoryViewObject* value_as_memoryview(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::MemoryView) {
    return nullptr;
  }
  return reinterpret_cast<MemoryViewObject*>(value.as.obj);
}

XLANG3_HOT_INLINE PropertyObject* value_as_property(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::Property) {
    return nullptr;
  }
  return reinterpret_cast<PropertyObject*>(value.as.obj);
}

struct FileObject {
  Object header;
  FileSystem* fs = nullptr;
  std::string path;
  std::string mode;
  std::string buffer;
  std::size_t cursor = 0;
  bool readable = false;
  bool writable = false;
  bool append = false;
  bool binary = false;
  bool closed = false;
};

XLANG3_HOT_INLINE FunctionObject* value_as_function(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::Function) {
    return nullptr;
  }
  return reinterpret_cast<FunctionObject*>(value.as.obj);
}

XLANG3_HOT_INLINE CodeObject* value_as_code(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::Code) {
    return nullptr;
  }
  return reinterpret_cast<CodeObject*>(value.as.obj);
}

XLANG3_HOT_INLINE FrameObject* value_as_frame(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::Frame) {
    return nullptr;
  }
  return reinterpret_cast<FrameObject*>(value.as.obj);
}

XLANG3_HOT_INLINE TracebackObject* value_as_traceback(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::Traceback) {
    return nullptr;
  }
  return reinterpret_cast<TracebackObject*>(value.as.obj);
}

XLANG3_HOT_INLINE NativeFunctionObject* value_as_native_function(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::NativeFunction) {
    return nullptr;
  }
  return reinterpret_cast<NativeFunctionObject*>(value.as.obj);
}

XLANG3_HOT_INLINE CellObject* value_as_cell(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::Cell) {
    return nullptr;
  }
  return reinterpret_cast<CellObject*>(value.as.obj);
}

void retain(const Value& value);
void release(const Value& value);

XLANG3_HOT_INLINE void value_release_if_object(Value& value) {
  if (value.tag == ValueTag::Object && value.as.obj != nullptr &&
      (value.flags & kXlangValueBorrowedRefFlag) == 0) {
    release(value);
  }
}

XLANG3_HOT_INLINE void value_assign_fast(Value& out, const Value& value) {
  if (&out == &value) {
    return;
  }
  if (value.tag == ValueTag::Object) {
    out = value;
    return;
  }
  value_release_if_object(out);
  out.tag = value.tag;
  out.flags = value.flags;
  out.as = value.as;
}

XLANG3_HOT_INLINE void value_move_assign_fast(Value& out, Value& value) {
  if (&out == &value) {
    return;
  }
  if (value.tag == ValueTag::Object && (value.flags & kXlangValueBorrowedRefFlag) != 0) {
    value_assign_fast(out, value);
    value.tag = ValueTag::Invalid;
    value.flags = 0;
    value.as.obj = nullptr;
    return;
  }
  value_release_if_object(out);
  out.tag = value.tag;
  out.flags = value.flags;
  out.as = value.as;
  value.tag = ValueTag::Invalid;
  value.flags = 0;
  value.as.obj = nullptr;
}

XLANG3_HOT_INLINE void value_borrow_assign_fast(Value& out, const Value& value) {
  if (&out == &value) {
    return;
  }
  value_release_if_object(out);
  out.tag = value.tag;
  out.flags = value.flags;
  out.as = value.as;
  if (out.tag == ValueTag::Object && out.as.obj != nullptr) {
    out.flags |= kXlangValueBorrowedRefFlag;
  }
}

XLANG3_HOT_INLINE void value_set_invalid(Value& out) {
  value_release_if_object(out);
  out.tag = ValueTag::Invalid;
  out.flags = 0;
  out.as.obj = nullptr;
}

XLANG3_HOT_INLINE void value_set_none(Value& out) {
  value_release_if_object(out);
  out.tag = ValueTag::None;
  out.flags = 0;
  out.as.obj = nullptr;
}

XLANG3_HOT_INLINE void value_set_bool(Value& out, bool value) {
  value_release_if_object(out);
  out.tag = ValueTag::Bool;
  out.flags = 0;
  out.as.b = value;
}

XLANG3_HOT_INLINE void value_set_int64(Value& out, int64_t value) {
  value_release_if_object(out);
  out.tag = ValueTag::Int64;
  out.flags = 0;
  out.as.i64 = value;
}

XLANG3_HOT_INLINE void value_set_number(Value& out, double value) {
  value_release_if_object(out);
  out.tag = ValueTag::Double;
  out.flags = 0;
  out.as.f64 = value;
}

std::string value_to_string(const Value& value);
bool value_truthy(const Value& value);

bool value_add(const Value& lhs, const Value& rhs, Value& out, std::string& error);
bool value_sub(const Value& lhs, const Value& rhs, Value& out, std::string& error);
bool value_mul(const Value& lhs, const Value& rhs, Value& out, std::string& error);
bool value_div(const Value& lhs, const Value& rhs, Value& out, std::string& error);
bool value_floor_div(const Value& lhs, const Value& rhs, Value& out, std::string& error);
bool value_mod(const Value& lhs, const Value& rhs, Value& out, std::string& error);
bool value_pow(const Value& lhs, const Value& rhs, Value& out, std::string& error);
bool value_bit_and(const Value& lhs, const Value& rhs, Value& out, std::string& error);
bool value_bit_or(const Value& lhs, const Value& rhs, Value& out, std::string& error);
bool value_bit_xor(const Value& lhs, const Value& rhs, Value& out, std::string& error);
bool value_shift_left(const Value& lhs, const Value& rhs, Value& out, std::string& error);
bool value_shift_right(const Value& lhs, const Value& rhs, Value& out, std::string& error);
bool value_invert(const Value& value, Value& out, std::string& error);
bool value_compare(const std::string& op, const Value& lhs, const Value& rhs, Value& out, std::string& error);
bool value_is(const Value& lhs, const Value& rhs);
bool value_contains(const Value& container, const Value& item, bool& out, std::string& error);

} // namespace xlang3
