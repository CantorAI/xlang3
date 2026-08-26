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
#include "xlang3/ir.h"
#include "xlang3/mapping.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/perf_counters.h"
#include "xlang3/sequence.h"
#include "xlang3/set_object.h"
#include "xlang3/value_hash.h"

#include "runtime/memory/x3_runtime_memory.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
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

struct BinaryCompareView {
  const char* data = nullptr;
  size_t size = 0;
};

BinaryCompareView binary_compare_view(const Value& value) {
  if (auto* bytes = value_as_bytes(value)) {
    const auto view = bytes_object_view(*bytes);
    return {view.data(), view.size()};
  }
  if (auto* bytearray = value_as_bytearray(value)) {
    return {bytearray->value.data(), bytearray->value.size()};
  }
  if (auto* memoryview = value_as_memoryview(value)) {
    if (memoryview->released) {
      return {};
    }
    const auto owner = binary_compare_view(memoryview->owner);
    if (owner.data == nullptr || memoryview->offset > owner.size || owner.size - memoryview->offset < memoryview->size) {
      return {};
    }
    return {owner.data + memoryview->offset, memoryview->size};
  }
  return {};
}

bool set_contains_value(const SetObject& set, const Value& value) {
  for (const auto& item : set.items) {
    if (value_key_equal(item, value)) {
      return true;
    }
  }
  return false;
}

bool add_iterable_to_set(Value& target, const Value& iterable, std::string& error) {
  Value iterator;
  if (!sequence_get_iter(iterable, iterator, error)) {
    return false;
  }
  while (true) {
    bool done = false;
    Value item;
    if (!sequence_iter_next(iterator, done, item, error)) {
      return false;
    }
    if (done) {
      return true;
    }
    if (!set_add(target, item, error)) {
      return false;
    }
  }
}

bool set_intersection_value(const Value& lhs, const Value& rhs, Value& out, std::string& error) {
  auto* left = value_as_set(lhs);
  if (left == nullptr) {
    error = "unsupported operands for set intersection";
    return false;
  }
  out = Value::set({});
  Value iterator;
  if (!sequence_get_iter(rhs, iterator, error)) {
    return false;
  }
  while (true) {
    bool done = false;
    Value item;
    if (!sequence_iter_next(iterator, done, item, error)) {
      return false;
    }
    if (done) {
      return true;
    }
    if (set_contains_value(*left, item) && !set_add(out, item, error)) {
      return false;
    }
  }
}

bool set_difference_value(const Value& lhs, const Value& rhs, Value& out, std::string& error) {
  auto* left = value_as_set(lhs);
  if (left == nullptr) {
    error = "unsupported operands for set difference";
    return false;
  }
  out = Value::set(left->items);
  auto* result = value_as_set(out);
  Value iterator;
  if (!sequence_get_iter(rhs, iterator, error)) {
    return false;
  }
  while (true) {
    bool done = false;
    Value item;
    if (!sequence_iter_next(iterator, done, item, error)) {
      return false;
    }
    if (done) {
      return true;
    }
    for (auto it = result->items.begin(); it != result->items.end(); ++it) {
      if (value_key_equal(*it, item)) {
        result->items.erase(it);
        break;
      }
    }
  }
}

bool set_symmetric_difference_value(const Value& lhs, const Value& rhs, Value& out, std::string& error) {
  auto* left = value_as_set(lhs);
  if (left == nullptr) {
    error = "unsupported operands for set symmetric difference";
    return false;
  }
  out = Value::set(left->items);
  auto* result = value_as_set(out);
  Value iterator;
  if (!sequence_get_iter(rhs, iterator, error)) {
    return false;
  }
  while (true) {
    bool done = false;
    Value item;
    if (!sequence_iter_next(iterator, done, item, error)) {
      return false;
    }
    if (done) {
      return true;
    }
    bool removed = false;
    for (auto it = result->items.begin(); it != result->items.end(); ++it) {
      if (value_key_equal(*it, item)) {
        result->items.erase(it);
        removed = true;
        break;
      }
    }
    if (!removed && !set_add(out, item, error)) {
      return false;
    }
  }
}

bool value_is_set_like_operand(const Value& value) {
  if (value_as_set(value) != nullptr) {
    return true;
  }
  if (auto* view = value_as_dict_view(value)) {
    return view->kind == DictIterationKind::Keys || view->kind == DictIterationKind::Items;
  }
  return false;
}

bool value_materialize_set_like(const Value& value, Value& out, std::string& error) {
  if (auto* set = value_as_set(value)) {
    out = Value::set(set->items);
    return true;
  }
  if (auto* view = value_as_dict_view(value)) {
    if (view->kind != DictIterationKind::Keys && view->kind != DictIterationKind::Items) {
      error = "dict_values view is not set-like";
      return false;
    }
    out = Value::set({});
    return add_iterable_to_set(out, value, error);
  }
  error = "object is not set-like";
  return false;
}

bool set_like_all_in(const Value& left_set_value, const Value& rhs, bool& out, std::string& error) {
  auto* left_set = value_as_set(left_set_value);
  if (left_set == nullptr) {
    error = "object is not a set";
    return false;
  }
  Value right_set_value;
  if (!value_materialize_set_like(rhs, right_set_value, error)) {
    return false;
  }
  auto* right_set = value_as_set(right_set_value);
  out = true;
  for (const auto& item : left_set->items) {
    if (!set_contains_value(*right_set, item)) {
      out = false;
      return true;
    }
  }
  return true;
}

bool set_like_compare_value(const std::string& op, const Value& lhs, const Value& rhs, Value& out, std::string& error) {
  Value left_set_value;
  Value right_set_value;
  if (!value_materialize_set_like(lhs, left_set_value, error) ||
      !value_materialize_set_like(rhs, right_set_value, error)) {
    return false;
  }
  auto* left_set = value_as_set(left_set_value);
  auto* right_set = value_as_set(right_set_value);

  bool left_in_right = false;
  if (!set_like_all_in(left_set_value, right_set_value, left_in_right, error)) {
    return false;
  }
  bool result = false;
  if (op == "==") {
    result = left_set->items.size() == right_set->items.size() && left_in_right;
  } else if (op == "!=") {
    result = left_set->items.size() != right_set->items.size() || !left_in_right;
  } else if (op == "<=") {
    result = left_in_right;
  } else if (op == "<") {
    result = left_set->items.size() < right_set->items.size() && left_in_right;
  } else if (op == ">=") {
    bool right_in_left = false;
    if (!set_like_all_in(right_set_value, left_set_value, right_in_left, error)) {
      return false;
    }
    result = right_in_left;
  } else if (op == ">") {
    bool right_in_left = false;
    if (!set_like_all_in(right_set_value, left_set_value, right_in_left, error)) {
      return false;
    }
    result = left_set->items.size() > right_set->items.size() && right_in_left;
  } else {
    error = "unknown comparison operator";
    return false;
  }
  value_set_bool(out, result);
  return true;
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
  obj->format = "B";
  obj->readonly = readonly;
  obj->released = false;
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
    std::vector<Value> defaults,
    std::vector<std::pair<std::string, Value>> kwdefaults,
    std::string qualname) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_object<FunctionObject>(ObjectKind::Function);
  obj->function_id = function_id;
  obj->closure = std::move(closure);
  obj->defaults = std::move(defaults);
  obj->kwdefaults = std::move(kwdefaults);
  if (module != nullptr && function_id < module->functions.size()) {
    const auto& fn = module->functions[function_id];
    obj->type_params = fn.type_params;
    if (!fn.doc.empty()) {
      obj->doc = Value::string(fn.doc);
    }
    for (const auto& param : fn.signature) {
      if ((param.kind == ir::ParamKind::PosOnly || param.kind == ir::ParamKind::PosOrKeyword) &&
          param.default_reg != UINT32_MAX &&
          param.default_reg < obj->defaults.size()) {
        obj->positional_defaults.push_back(obj->defaults[param.default_reg]);
      }
    }
  } else {
    obj->positional_defaults = obj->defaults;
  }
  obj->globals_module = std::move(globals_module);
  obj->attrs_dict = Value::dict({});
  if (qualname.empty() && module != nullptr && function_id < module->functions.size()) {
    qualname = module->functions[function_id].qualname.empty() ? module->functions[function_id].name
                                                               : module->functions[function_id].qualname;
  }
  obj->qualname = std::move(qualname);
  obj->module = std::move(module);
  v.as.obj = &obj->header;
  return v;
}

Value Value::code(std::shared_ptr<const ir::Module> module, uint32_t function_id, std::string mode) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_object<CodeObject>(ObjectKind::Code);
  obj->module = std::move(module);
  obj->function_id = function_id;
  obj->mode = std::move(mode);
  v.as.obj = &obj->header;
  return v;
}

Value Value::frame(
    std::shared_ptr<const ir::Module> module,
    uint32_t function_id,
    Value globals_module,
    uint32_t instruction_index,
    Value locals,
    Value back,
    Value builtins) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_object<FrameObject>(ObjectKind::Frame);
  obj->module = std::move(module);
  obj->function_id = function_id;
  obj->instruction_index = instruction_index;
  obj->globals_module = std::move(globals_module);
  obj->locals = std::move(locals);
  obj->back = std::move(back);
  obj->builtins = std::move(builtins);
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

Value Value::type_param(std::string name) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_object<TypeParamObject>(ObjectKind::TypeParam);
  obj->name = std::move(name);
  obj->bound = Value::none();
  obj->default_value = Value::none();
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
    case ObjectKind::AsyncGeneratorAwaitable:
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
      delete as_native_function(value.as.obj)->attrs_dict;
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
    case ObjectKind::StaticMethod:
    case ObjectKind::ClassMethod:
    case ObjectKind::Super:
    case ObjectKind::SlotDescriptor:
      object_model_release_object(value.as.obj);
      break;
    case ObjectKind::Property:
      delete value_as_property(value);
      break;
    case ObjectKind::File:
      delete as_file(value.as.obj);
      break;
    case ObjectKind::TypeParam:
      delete value_as_type_param(value);
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
      if (value.as.obj != nullptr &&
          (value.as.obj->kind == ObjectKind::Generator ||
           value.as.obj->kind == ObjectKind::AsyncGeneratorAwaitable)) {
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
      if (value.as.obj != nullptr && value.as.obj->kind == ObjectKind::SlotDescriptor) {
        return object_model_to_string(value);
      }
      if (value.as.obj != nullptr && value.as.obj->kind == ObjectKind::TypeParam) {
        return "<type parameter " + value_as_type_param(value)->name + ">";
      }
      if (value.as.obj != nullptr &&
          (value.as.obj->kind == ObjectKind::Class ||
           value.as.obj->kind == ObjectKind::Instance ||
           value.as.obj->kind == ObjectKind::BoundMethod ||
           value.as.obj->kind == ObjectKind::StaticMethod ||
           value.as.obj->kind == ObjectKind::ClassMethod ||
           value.as.obj->kind == ObjectKind::Super)) {
        return object_model_to_string(value);
      }
      if (value.as.obj != nullptr && value.as.obj->kind == ObjectKind::File) {
        return "<file '" + as_file(value.as.obj)->path + "'>";
      }
      return "<object>";
  }
  return "<unknown>";
}

bool string_percent_arg(
    const Value& args,
    size_t& tuple_index,
    const std::string& mapping_key,
    Value& out,
    std::string& error) {
  if (!mapping_key.empty()) {
    if (!mapping_get_item(args, Value::string(mapping_key), out, error)) {
      error = "format mapping key '" + mapping_key + "' not found";
      return false;
    }
    return true;
  }
  if (auto* tuple = value_as_tuple(args)) {
    if (tuple_index >= tuple->items.size()) {
      error = "not enough arguments for format string";
      return false;
    }
    value_assign_fast(out, tuple->items[tuple_index++]);
    return true;
  }
  if (tuple_index != 0) {
    error = "not enough arguments for format string";
    return false;
  }
  value_assign_fast(out, args);
  ++tuple_index;
  return true;
}

bool string_percent_format(const Value& lhs, const Value& rhs, Value& out, std::string& error) {
  auto* format_object = value_as_string(lhs);
  if (format_object == nullptr) {
    return false;
  }

  const auto format = string_object_view(*format_object);
  std::string result;
  result.reserve(format.size());
  size_t tuple_index = 0;

  for (size_t i = 0; i < format.size(); ++i) {
    const char ch = format[i];
    if (ch != '%') {
      result.push_back(ch);
      continue;
    }
    if (i + 1 >= format.size()) {
      error = "incomplete format";
      return false;
    }
    if (format[i + 1] == '%') {
      result.push_back('%');
      ++i;
      continue;
    }

    std::string mapping_key;
    ++i;
    if (format[i] == '(') {
      const size_t key_start = i + 1;
      const auto key_end = format.find(')', key_start);
      if (key_end == std::string_view::npos) {
        error = "incomplete format key";
        return false;
      }
      mapping_key.assign(format.substr(key_start, key_end - key_start));
      i = key_end + 1;
      if (i >= format.size()) {
        error = "incomplete format";
        return false;
      }
    }

    while (i < format.size() && std::strchr("#0- +", format[i]) != nullptr) {
      ++i;
    }
    while (i < format.size() && std::isdigit(static_cast<unsigned char>(format[i]))) {
      ++i;
    }
    if (i < format.size() && format[i] == '.') {
      ++i;
      while (i < format.size() && std::isdigit(static_cast<unsigned char>(format[i]))) {
        ++i;
      }
    }
    if (i < format.size() && (format[i] == 'h' || format[i] == 'l' || format[i] == 'L')) {
      ++i;
    }
    if (i >= format.size()) {
      error = "incomplete format";
      return false;
    }

    Value arg;
    if (!string_percent_arg(rhs, tuple_index, mapping_key, arg, error)) {
      return false;
    }

    switch (format[i]) {
      case 's':
      case 'r':
      case 'a':
        result += value_to_string(arg);
        break;
      case 'd':
      case 'i':
      case 'u':
        if (arg.tag != ValueTag::Int64) {
          error = "%d format requires an integer";
          return false;
        }
        result += std::to_string(arg.as.i64);
        break;
      case 'f':
      case 'F':
      case 'g':
      case 'G':
      case 'e':
      case 'E':
        if (!is_number(arg)) {
          error = "%f format requires a number";
          return false;
        }
#if defined(XLANG3_EMBEDDED)
        result += format_f64(as_double(arg));
#else
        result += std::to_string(as_double(arg));
#endif
        break;
      default:
        error = "unsupported format character";
        return false;
    }
  }

  if (auto* tuple = value_as_tuple(rhs); tuple != nullptr && tuple_index < tuple->items.size()) {
    error = "not all arguments converted during string formatting";
    return false;
  }
  out = Value::string(std::move(result));
  return true;
}

bool const_bool_method_value(const Value& method, bool& out) {
  auto* function_object = value_as_function(method);
  if (function_object == nullptr || function_object->module == nullptr) {
    return false;
  }
  const auto& module = *function_object->module;
  if (function_object->function_id >= module.functions.size()) {
    return false;
  }
  const auto& function = module.functions[function_object->function_id];
  if (function.params.size() != 1 || !function.free_vars.empty() || !function.cell_slots.empty() ||
      function.code.size() < 2) {
    return false;
  }
  const auto& load_const = function.code[0];
  const auto& ret = function.code[1];
  if (load_const.op != ir::Op::LoadConst || load_const.a >= function.constants.size() ||
      ret.op != ir::Op::Return || ret.a != load_const.dst) {
    return false;
  }
  const auto& value = function.constants[load_const.a];
  if (value.tag != ValueTag::Bool) {
    return false;
  }
  out = value.as.b;
  return true;
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
      if (value.as.obj != nullptr &&
          (value.as.obj->kind == ObjectKind::Generator ||
           value.as.obj->kind == ObjectKind::AsyncGeneratorAwaitable)) {
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
      if (value.as.obj != nullptr && value.as.obj->kind == ObjectKind::Instance) {
        Value bool_method;
        std::string ignored;
        if (object_get_class_attr_for_instance(value, "__bool__", bool_method, ignored)) {
          bool out = true;
          if (const_bool_method_value(bool_method, out)) {
            return out;
          }
        }
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
  if (auto* left = value_as_list(lhs)) {
    if (auto* right = value_as_list(rhs)) {
      std::vector<Value> items;
      items.reserve(left->items.size() + right->items.size());
      for (const auto& item : left->items) {
        items.push_back(item);
      }
      for (const auto& item : right->items) {
        items.push_back(item);
      }
      out = Value::list(std::move(items));
      return true;
    }
  }
  if (auto* left = value_as_tuple(lhs)) {
    if (auto* right = value_as_tuple(rhs)) {
      std::vector<Value> items;
      items.reserve(left->items.size() + right->items.size());
      for (const auto& item : left->items) {
        items.push_back(item);
      }
      for (const auto& item : right->items) {
        items.push_back(item);
      }
      out = Value::tuple(std::move(items));
      return true;
    }
  }
  error = "unsupported operands for +";
  return false;
}

bool value_sub(const Value& lhs, const Value& rhs, Value& out, std::string& error) {
  if (value_is_set_like_operand(lhs)) {
    Value left_set;
    if (!value_materialize_set_like(lhs, left_set, error)) {
      return false;
    }
    return set_difference_value(left_set, rhs, out, error);
  }
  if (value_as_set(lhs) != nullptr) {
    return set_difference_value(lhs, rhs, out, error);
  }
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
  auto repeat_string = [&](const StringObject* text, int64_t count) {
    if (count <= 0) {
      out = Value::string("");
      return true;
    }
    const auto view = string_object_view(*text);
    std::string repeated;
    repeated.reserve(view.size() * static_cast<size_t>(count));
    for (int64_t i = 0; i < count; ++i) {
      repeated.append(view.data(), view.size());
    }
    out = Value::string(std::move(repeated));
    return true;
  };
  if (auto* text = value_as_string(lhs)) {
    if (rhs.tag == ValueTag::Int64) {
      return repeat_string(text, rhs.as.i64);
    }
  }
  if (auto* text = value_as_string(rhs)) {
    if (lhs.tag == ValueTag::Int64) {
      return repeat_string(text, lhs.as.i64);
    }
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
  if (value_as_string(lhs) != nullptr) {
    return string_percent_format(lhs, rhs, out, error);
  }
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
  if (value_is_set_like_operand(lhs)) {
    Value left_set;
    if (!value_materialize_set_like(lhs, left_set, error)) {
      return false;
    }
    return set_intersection_value(left_set, rhs, out, error);
  }
  if (value_as_set(lhs) != nullptr) {
    return set_intersection_value(lhs, rhs, out, error);
  }
  if (lhs.tag != ValueTag::Int64 || rhs.tag != ValueTag::Int64) {
    error = "unsupported operands for &";
    return false;
  }
  value_set_int64(out, lhs.as.i64 & rhs.as.i64);
  return true;
}

bool value_bit_or(const Value& lhs, const Value& rhs, Value& out, std::string& error) {
  if (value_as_dict(lhs) != nullptr && value_as_dict(rhs) != nullptr) {
    auto* left = value_as_dict(lhs);
    auto* right = value_as_dict(rhs);
    std::vector<std::pair<Value, Value>> entries;
    entries.reserve(left->entries.size() + right->entries.size());
    for (const auto& entry : left->entries) {
      entries.push_back(entry);
    }
    out = Value::dict(std::move(entries));
    for (const auto& entry : right->entries) {
      if (!mapping_set_item(out, entry.first, entry.second, error)) {
        return false;
      }
    }
    return true;
  }
  if (value_as_set(lhs) != nullptr) {
    out = Value::set(value_as_set(lhs)->items);
    return add_iterable_to_set(out, rhs, error);
  }
  if (auto* view = value_as_dict_view(lhs)) {
    if (view->kind == DictIterationKind::Keys || view->kind == DictIterationKind::Items) {
      if (!value_materialize_set_like(lhs, out, error)) {
        return false;
      }
      return add_iterable_to_set(out, rhs, error);
    }
  }
  const auto type_like = [](const Value& value) {
    if (value.tag == ValueTag::None) {
      return true;
    }
    if (value_as_class(value) != nullptr ||
        value_as_function(value) != nullptr ||
        value_as_native_function(value) != nullptr ||
        instance_get_native_data(value, "typing._Alias") != nullptr) {
      return true;
    }
    if (value.tag == ValueTag::Object && value.as.obj != nullptr && value.as.obj->kind == ObjectKind::Tuple) {
      return true;
    }
    return false;
  };
  if ((lhs.tag != ValueTag::Int64 || rhs.tag != ValueTag::Int64) &&
      type_like(lhs) && type_like(rhs)) {
    out = Value::tuple({lhs, rhs});
    return true;
  }
  if (lhs.tag != ValueTag::Int64 || rhs.tag != ValueTag::Int64) {
    error = "unsupported operands for |";
    return false;
  }
  value_set_int64(out, lhs.as.i64 | rhs.as.i64);
  return true;
}

bool value_bit_xor(const Value& lhs, const Value& rhs, Value& out, std::string& error) {
  if (value_is_set_like_operand(lhs)) {
    Value left_set;
    if (!value_materialize_set_like(lhs, left_set, error)) {
      return false;
    }
    return set_symmetric_difference_value(left_set, rhs, out, error);
  }
  if (value_as_set(lhs) != nullptr) {
    return set_symmetric_difference_value(lhs, rhs, out, error);
  }
  const bool lhs_int = lhs.tag == ValueTag::Int64 || lhs.tag == ValueTag::Bool;
  const bool rhs_int = rhs.tag == ValueTag::Int64 || rhs.tag == ValueTag::Bool;
  if (!lhs_int || !rhs_int) {
    error = "unsupported operands for ^";
    return false;
  }
  const int64_t lhs_value = lhs.tag == ValueTag::Bool ? (lhs.as.b ? 1 : 0) : lhs.as.i64;
  const int64_t rhs_value = rhs.tag == ValueTag::Bool ? (rhs.as.b ? 1 : 0) : rhs.as.i64;
  if (lhs.tag == ValueTag::Bool && rhs.tag == ValueTag::Bool) {
    value_set_bool(out, (lhs_value ^ rhs_value) != 0);
  } else {
    value_set_int64(out, lhs_value ^ rhs_value);
  }
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
  auto* left_view = value_as_dict_view(lhs);
  auto* right_view = value_as_dict_view(rhs);
  if ((left_view != nullptr && left_view->kind == DictIterationKind::Values) ||
      (right_view != nullptr && right_view->kind == DictIterationKind::Values)) {
    if (op == "==" || op == "!=") {
      result = lhs.tag == ValueTag::Object && rhs.tag == ValueTag::Object && lhs.as.obj == rhs.as.obj;
      value_set_bool(out, op == "==" ? result : !result);
      return true;
    }
    error = "unsupported comparison";
    return false;
  }
  if (value_is_set_like_operand(lhs) && value_is_set_like_operand(rhs)) {
    return set_like_compare_value(op, lhs, rhs, out, error);
  }
  if (lhs.tag == ValueTag::Object && rhs.tag == ValueTag::Object &&
      lhs.as.obj != nullptr && rhs.as.obj != nullptr &&
      lhs.as.obj->kind == ObjectKind::Tuple && rhs.as.obj->kind == ObjectKind::Tuple) {
    const auto* left = reinterpret_cast<const TupleObject*>(lhs.as.obj);
    const auto* right = reinterpret_cast<const TupleObject*>(rhs.as.obj);
    const size_t common = std::min(left->items.size(), right->items.size());
    int ordering = 0;
    for (size_t i = 0; i < common; ++i) {
      Value equal;
      if (!value_compare("==", left->items[i], right->items[i], equal, error)) {
        return false;
      }
      if (equal.tag == ValueTag::Bool && equal.as.b) {
        continue;
      }
      Value less;
      if (!value_compare("<", left->items[i], right->items[i], less, error)) {
        return false;
      }
      ordering = (less.tag == ValueTag::Bool && less.as.b) ? -1 : 1;
      break;
    }
    if (ordering == 0 && left->items.size() != right->items.size()) {
      ordering = left->items.size() < right->items.size() ? -1 : 1;
    }
    if (op == "==") result = ordering == 0;
    else if (op == "!=") result = ordering != 0;
    else if (op == "<") result = ordering < 0;
    else if (op == "<=") result = ordering <= 0;
    else if (op == ">") result = ordering > 0;
    else if (op == ">=") result = ordering >= 0;
    else {
      error = "unknown comparison operator";
      return false;
    }
    value_set_bool(out, result);
    return true;
  }
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
  if (auto* left_string = value_as_string(lhs)) {
    if (auto* right_string = value_as_string(rhs)) {
      const auto ordering = string_object_view(*left_string).compare(string_object_view(*right_string));
      if (op == "==") result = ordering == 0;
      else if (op == "!=") result = ordering != 0;
      else if (op == "<") result = ordering < 0;
      else if (op == "<=") result = ordering <= 0;
      else if (op == ">") result = ordering > 0;
      else if (op == ">=") result = ordering >= 0;
      else {
        error = "unknown comparison operator";
        return false;
      }
      value_set_bool(out, result);
      return true;
    }
  }
  const auto left_binary_order = binary_compare_view(lhs);
  const auto right_binary_order = binary_compare_view(rhs);
  if (left_binary_order.data != nullptr && right_binary_order.data != nullptr) {
    const int cmp = std::memcmp(
        left_binary_order.data,
        right_binary_order.data,
        std::min(left_binary_order.size, right_binary_order.size));
    const int ordering = cmp != 0 ? cmp :
        (left_binary_order.size < right_binary_order.size ? -1 :
            (left_binary_order.size > right_binary_order.size ? 1 : 0));
    if (op == "==") result = ordering == 0;
    else if (op == "!=") result = ordering != 0;
    else if (op == "<") result = ordering < 0;
    else if (op == "<=") result = ordering <= 0;
    else if (op == ">") result = ordering > 0;
    else if (op == ">=") result = ordering >= 0;
    else {
      error = "unknown comparison operator";
      return false;
    }
    value_set_bool(out, result);
    return true;
  }
  if (op == "==" || op == "!=") {
    const auto left_binary = binary_compare_view(lhs);
    const auto right_binary = binary_compare_view(rhs);
    if (left_binary.data != nullptr && right_binary.data != nullptr) {
      result = left_binary.size == right_binary.size &&
               (left_binary.size == 0 || std::memcmp(left_binary.data, right_binary.data, left_binary.size) == 0);
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
  if (auto* instance = value_as_instance(container)) {
    if (auto* dict = value_as_dict(instance->mapping_storage)) {
      for (const auto& entry : dict->entries) {
        if (value_key_equal(entry.first, item)) {
          out = true;
          return true;
        }
      }
      return true;
    }
  }
  if (value_as_dict_view(container) != nullptr || value_as_module(container) != nullptr) {
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
