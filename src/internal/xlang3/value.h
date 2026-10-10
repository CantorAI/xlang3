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
#include <mutex>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace xlang3 {

struct CallArgsView;
class XlangRuntimeExecutionGuard;

namespace ir {
struct Module;
}

namespace memory {
class X3BucketAllocator;
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
  BigInt,
  Complex,
  Bytes,
  ByteArray,
  MemoryView,
  Slice,
  Tuple,
  List,
  Dict,
  MappingProxy,
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
  ZipLongestIterator,
  MapIterator,
  FilterIterator,
  CallableIterator,
  ChainIterator,
  ProtocolIterator,
  Generator,
  AsyncGeneratorAwaitable,
  Module,
  Cell,
  Function,
  NativeFunction,
  Class,
  Instance,
  BoundMethod,
  StaticMethod,
  ClassMethod,
  Super,
  SlotDescriptor,
  Property,
  Event,
  Code,
  Frame,
  Traceback,
  File,
  GenericAlias,
  TypeParam,
  Expression,
};

// The tracking index needs only 62 bits in practice. Share the remaining
// state word with weakref-role bits so the no-weakref fast path does not grow
// every heap object's header.
constexpr uint64_t kGcObjectIndexMask = (uint64_t{1} << 62) - 1;
constexpr uint64_t kGcObjectIndexNone = kGcObjectIndexMask;
constexpr uint64_t kObjectWeakrefTargetFlag = uint64_t{1} << 63;
constexpr uint64_t kObjectWeakrefReferenceFlag = uint64_t{1} << 62;
constexpr uint64_t kObjectWeakrefFlagsMask =
    kObjectWeakrefTargetFlag | kObjectWeakrefReferenceFlag;

struct Object {
  ObjectKind kind;
  std::atomic_uint32_t refcnt;
  // GC index (low bits) keeps O(1) removal; weakref roles (high bits) let
  // ordinary destruction bypass the registry lock without enlarging Object.
  std::atomic_uint64_t gc_tracking_state{kGcObjectIndexNone};
};

void gc_track_object(Object* object);
void gc_untrack_object(Object* object);

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

// Some native keyword methods have one stable, common shape that can consume
// VM registers directly. The predicate is side-effect free and runs before
// monitoring events; the callback runs after the call is observable. It owns
// lock management and must copy any VM-backed values before releasing the
// execution lock for runtime re-entry.
using NativeFastKeywordPredicate = bool (*)(const CallArgsView& values);
using NativeFastKeywordCallCallback = bool (*)(
    Runtime& runtime,
    const CallArgsView& values,
    XlangRuntimeExecutionGuard& execution_lock,
    Value& out,
    std::string& error,
    void* user_data);

struct NativeFunctionObject {
  Object header;
  uint32_t native_id = 0;
  uint32_t specialization_id = 0;
  std::string name;
  NativeFunctionCallback callback = nullptr;
  NativeKeywordFunctionCallback keyword_callback = nullptr;
  NativeFastCallCallback fast_callback = nullptr;
  NativeFastKeywordPredicate fast_keyword_predicate = nullptr;
  NativeFastKeywordCallCallback fast_keyword_callback = nullptr;
  bool fast_releases_vm_lock = false;
  bool bind_as_descriptor = true;
  bool capture_expressions = false;
  bool ipc_args_by_value = false;
  void* user_data = nullptr;
  void (*user_data_cleanup)(void*) = nullptr;
  Value* attrs_dict = nullptr;
};

inline constexpr uintptr_t kStringAsciiFlag = 1;
inline constexpr uintptr_t kStringImmortalFlag = 2;
inline constexpr uintptr_t kStringMetadataFlags = 3;

struct StringObject {
  Object header;
  uint32_t size = 0;
  uint32_t alloc_size = 0;
  memory::X3BucketAllocator* allocator = nullptr;
  // Strings are immutable after construction. Cache their hash so repeated
  // mapping and set lookups do not rescan the bytes.
  mutable std::atomic_size_t cached_hash{static_cast<size_t>(-1)};
  // Keep the lazy Unicode index in the former flags/padding word. Growing
  // every ASCII string header just to accelerate rare Unicode inputs would
  // penalize allocations across otherwise unrelated Python workloads.
  // Index pointers are aligned; only their two low bits hold ASCII/immortal.
  mutable std::atomic_uintptr_t unicode_metadata{0};
  // Immutable string bytes follow this object in the same allocation block.
};

struct BigIntObject {
  Object header;
  void* impl = nullptr;
};

struct ComplexObject {
  Object header;
  double real = 0.0;
  double imag = 0.0;
};

struct BytesObject {
  Object header;
  uint32_t size = 0;
  uint32_t alloc_size = 0;
  memory::X3BucketAllocator* allocator = nullptr;
  mutable std::atomic<size_t> cached_hash{static_cast<size_t>(-1)};
  // Immutable bytes follow this object in the same allocation block.
};

struct ByteArrayObject {
  Object header;
  std::string value;
  size_t buffer_exports = 0;
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
  static Value bigint_from_i64(int64_t value);
  static Value number(double value);
  static Value complex(double real, double imag);
  static Value string(std::string value);
  static Value string_view(std::string_view value);
  static Value string_uninitialized(size_t size);
  static Value bytes(std::string_view value);
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
  static Value frozenset(std::vector<Value> items);
  static Value range(int64_t start, int64_t stop, int64_t step);
  static Value range_values(Value start, Value stop, Value step);
  static Value range_iterator(int64_t current, int64_t stop, int64_t step);
  static Value range_iterator_values(Value current, Value stop, Value step);
  static Value sequence_iterator(Value source, uint64_t index);
  static Value generator(
      Runtime* runtime,
      Value function,
      std::vector<Value> args,
      bool is_async = false,
      bool is_coroutine = false,
      bool args_bound = false);
  static Value module(std::string name);
  static Value cell(Value value);
  static Value function(uint32_t function_id, std::vector<Value> closure);
  static Value function(uint32_t function_id, std::vector<Value> closure, Value globals_module);
  static Value function(
      uint32_t function_id,
      std::vector<Value> closure,
      Value globals_module,
      std::shared_ptr<const ir::Module> module,
      std::vector<Value> defaults = {},
      std::vector<std::pair<std::string, Value>> kwdefaults = {},
      std::string qualname = {});
  static Value code(std::shared_ptr<const ir::Module> module, uint32_t function_id, std::string mode = "exec");
  static Value frame(
      std::shared_ptr<const ir::Module> module,
      uint32_t function_id,
      Value globals_module,
      uint32_t instruction_index = 0,
      Value locals = Value::invalid(),
      Value back = Value::invalid(),
      Value builtins = Value::invalid(),
      uint64_t activation_id = 0);
  static Value traceback(Value frame, Value next, int64_t line, int64_t lasti = -2);
  static Value native_function(
      uint32_t native_id,
      std::string name,
      NativeFunctionCallback callback,
      void* user_data = nullptr,
      void (*user_data_cleanup)(void*) = nullptr,
      NativeFastCallCallback fast_callback = nullptr,
      bool fast_releases_vm_lock = false,
      NativeKeywordFunctionCallback keyword_callback = nullptr,
      bool bind_as_descriptor = true);
  static Value file(FileSystem* fs, std::string path, std::string mode, std::string buffer, bool writable);
  static Value fd_file(int fd, std::string name, std::string mode, bool readable, bool writable, bool binary, bool closefd);
  static Value class_object(
      std::string name,
      std::vector<std::pair<std::string, Value>> attrs,
      Value base = Value::invalid(),
      std::vector<std::string> instance_slots = {},
      Value metaclass = Value::invalid(),
      Value globals_module = Value::invalid());
  static Value instance(Value klass);
  static Value bound_method(Value self, Value function);
  static Value static_method(Value function);
  static Value class_method(Value function);
  static Value super_object(Value klass, Value self);
  static Value property(Value fget, Value fset, Value fdel, Value doc, bool is_abstract = false, bool doc_from_getter = false);
  static Value event(std::string name);
  static Value generic_alias(Value origin, Value args);
  static Value union_type(std::vector<Value> args);
  static Value type_param(std::string name);
};

bool gc_value_is_tracked(const Value& value);
std::vector<Value> gc_snapshot_tracked_objects();

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

Value value_bigint_from_i64(int64_t value);
Value value_bigint_from_u64(uint64_t value);
Value value_bigint_from_decimal(std::string_view text, int base, std::string& error);
bool value_bigint_from_bytes(const uint8_t* bytes, size_t size, bool is_big, bool signed_value, Value& out, std::string& error);
bool value_int_like_to_bytes(const Value& value, size_t length, bool is_big, bool signed_value, std::string& out, std::string& error);
void value_bigint_destroy(BigIntObject* value);
std::string value_bigint_to_string(const Value& value);
bool value_bigint_truthy(const Value& value);
bool value_bigint_to_i64(const Value& value, int64_t& out);
bool value_bigint_to_u64(const Value& value, uint64_t& out);
bool value_bigint_limb_view(const Value& value, bool& negative, const uint32_t*& limbs, uint32_t& count);
bool value_bigint_from_binary_limbs(const void* data, size_t bytes, bool negative, Value& out, std::string& error);
bool value_int_like_to_i64(const Value& value, int64_t& out);
bool value_int_like_bit_length(const Value& value, int64_t& out);
bool value_int_like_hash(const Value& value, size_t& out);
bool value_int_like_compare(const std::string& op, const Value& lhs, const Value& rhs, Value& out);
bool value_int_like_add(const Value& lhs, const Value& rhs, Value& out);
bool value_int_like_sub(const Value& lhs, const Value& rhs, Value& out);
bool value_int_like_mul(const Value& lhs, const Value& rhs, Value& out);
bool value_int_like_divmod(const Value& lhs, const Value& rhs, Value& quotient, Value& remainder,
                           std::string& error);
bool value_int_like_pow(const Value& lhs, const Value& rhs, Value& out, std::string& error);
bool value_int_like_bit_and(const Value& lhs, const Value& rhs, Value& out);
bool value_int_like_bit_or(const Value& lhs, const Value& rhs, Value& out);
bool value_int_like_bit_xor(const Value& lhs, const Value& rhs, Value& out);
bool value_int_like_shift_left(const Value& lhs, const Value& rhs, Value& out, std::string& error);
bool value_int_like_shift_right(const Value& lhs, const Value& rhs, Value& out, std::string& error);
bool value_int_like_invert(const Value& value, Value& out);

XLANG3_HOT_INLINE BigIntObject* value_as_bigint(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::BigInt) {
    return nullptr;
  }
  return reinterpret_cast<BigIntObject*>(value.as.obj);
}

XLANG3_HOT_INLINE ComplexObject* value_as_complex(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::Complex) {
    return nullptr;
  }
  return reinterpret_cast<ComplexObject*>(value.as.obj);
}

XLANG3_HOT_INLINE Value Value::bigint_from_i64(int64_t value) {
  return value_bigint_from_i64(value);
}

XLANG3_HOT_INLINE uint32_t next_float_identity() {
  static std::atomic<uint32_t> next{1};
  uint32_t identity = next.fetch_add(1, std::memory_order_relaxed) & 0x3fffffffu;
  if (identity == 0) identity = next.fetch_add(1, std::memory_order_relaxed) & 0x3fffffffu;
  return identity;
}

XLANG3_HOT_INLINE Value Value::number(double value) {
  Value v;
  v.tag = ValueTag::Double;
  v.flags = next_float_identity();
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
  memory::X3BucketAllocator* allocator = nullptr;
  TupleItems items;
  // Only completed, callback-free immutable keys publish this cache. Runtime
  // and non-runtime hashing disagree for user objects, so their hashes must
  // never enter this shared slot. Atomics permit simultaneous read-only hashes.
  mutable std::atomic<size_t> cached_intrinsic_hash{static_cast<size_t>(-1)};
  bool construction_complete = false;
};

XLANG3_HOT_INLINE void tuple_object_begin_construction(TupleObject& object) {
  object.cached_intrinsic_hash.store(static_cast<size_t>(-1), std::memory_order_relaxed);
  object.construction_complete = false;
}

XLANG3_HOT_INLINE void tuple_object_complete_construction(TupleObject& object) {
  object.cached_intrinsic_hash.store(static_cast<size_t>(-1), std::memory_order_relaxed);
  object.construction_complete = true;
}

struct CellObject {
  Object header;
  Value value;
};

struct FunctionObject {
  Object header;
  uint32_t function_id = 0;
  // Body-derived VM specializations retain this generation. __code__ writes
  // advance it so cached signatures/IR plans rebuild without disabling inlining.
  uint64_t code_version = 0;
  // Lazily materialized identity, or the exact assigned code object (including
  // code.replace() metadata such as CO_ITERABLE_COROUTINE).
  Value code_object;
  std::vector<Value> closure;
  std::vector<Value> defaults;
  std::vector<Value> positional_defaults;
  std::vector<std::pair<std::string, Value>> kwdefaults;
  // Invalid keeps ordinary definitions on indexed defaults without allocating
  // a dict. Once exposed/assigned, this is the authoritative live dict (or
  // None); aliases and mutations must affect the next keyword-only binding.
  Value kwdefaults_dict;
  std::vector<std::string> type_params;
  Value annotations;
  Value doc;
  Value globals_module;
  // Captured builtins namespace, matching function.__builtins__; keeping it
  // separate avoids allocating an attribute dictionary for every function.
  Value builtins;
  Value globals_dict;
  Value attrs_dict;
  std::shared_ptr<const ir::Module> module;
  std::string qualname;
};

struct TypeParamObject {
  Object header;
  std::string name;
  Value bound;
  Value default_value;
};

struct GenericAliasObject {
  Object header;
  Value origin;
  Value args;
  Value klass;
  bool is_union = false;
};

struct CodeObject {
  Object header;
  std::shared_ptr<const ir::Module> module;
  uint32_t function_id = 0;
  std::string mode;
  std::string filename_override;
  std::string name_override;
  std::string qualname_override;
  int64_t first_line_override = 0;
  int64_t flags_override = -1;
};

struct FrameObject {
  Object header;
  std::shared_ptr<const ir::Module> module;
  uint32_t function_id = 0;
  uint32_t instruction_index = 0;
  // Coverage repeatedly reads f_code on its observed frame. Materialize that
  // immutable module/function pair once per Python frame object, without adding
  // owners or metadata to ordinary VM frames. This Value dies with the frame.
  Value code_object;
  Value globals_module;
  Value locals;
  // Traceback frames retain local values compactly and construct the Python
  // f_locals dict only if code actually asks for it.
  std::vector<Value> local_snapshot;
  bool has_lazy_locals = false;
  Value back;
  Value builtins;
  Value trace;
  Value generator_ref;
  uint64_t activation_id = 0;
  int64_t owner_thread_ident = 0;
  bool trace_lines = true;
  bool trace_opcodes = false;
  bool allow_line_jump = false;
  bool source_line_is_current = false;
  bool live = false;
  bool refresh_instruction = true;
};

struct NativeBufferStorage {
  char* data = nullptr;
  size_t size = 0;
  void* context = nullptr;
  void (*cleanup)(void*) = nullptr;
  ~NativeBufferStorage() { if (cleanup) cleanup(context); }
};

struct MemoryViewObject {
  Object header;
  Value owner;
  Value exporter;
  std::shared_ptr<NativeBufferStorage> external;
  size_t offset = 0;
  size_t size = 0;
  std::string format = "B";
  std::vector<int64_t> shape;
  std::vector<int64_t> strides;
  bool readonly = true;
  bool contiguous = true;
  bool released = false;
  bool owns_bytearray_export = false;
};

void frame_materialize_locals(FrameObject& frame);

// Owner span is physical storage; object_view is only a C-contiguous buffer.
// Strided element access uses owner coordinates, while byte conversion packs
// logical elements explicitly instead of detaching a readonly snapshot.
std::string_view memoryview_owner_view(const MemoryViewObject& view);
char* memoryview_owner_writable_data(const MemoryViewObject& view);
bool memoryview_copy_bytes(const MemoryViewObject& view, std::string& out, std::string& error);
std::string_view memoryview_object_view(const MemoryViewObject& view);
char* memoryview_object_writable_data(const MemoryViewObject& view);

XLANG3_HOT_INLINE size_t memoryview_format_itemsize(std::string_view format) {
  if (format.empty()) {
    return 1;
  }
  switch (format.back()) {
    case 'B':
    case 'b':
    case 'c':
    case '?':
      return 1;
    case 'H':
    case 'h':
      return 2;
    case 'I':
    case 'i':
    case 'f':
    case 'w':
      return 4;
    case 'L':
    case 'l':
      return sizeof(long);
    case 'u':
      return sizeof(wchar_t);
    case 'Q':
    case 'q':
    case 'd':
      return 8;
    case 'N':
    case 'n':
      return sizeof(void*);
    default:
      return 0;
  }
}

XLANG3_HOT_INLINE size_t memoryview_item_count(const MemoryViewObject& view) {
  const size_t itemsize = memoryview_format_itemsize(view.format);
  return itemsize == 0 ? view.size : view.size / itemsize;
}

struct PropertyObject {
  Object header;
  Runtime* native_module_runtime = nullptr;
  Value fget;
  Value fset;
  Value fdel;
  Value doc;
  Value name;
  bool is_abstract = false;
  bool doc_from_getter = false;
  bool has_name = false;
  bool name_from_getter = false;
};

struct EventHandlerObject {
  uint64_t cookie = 0;
  Value callable;
};

struct EventObject {
  Object header;
  std::string name;
  uint64_t next_cookie = 1;
  std::vector<EventHandlerObject> handlers;
  std::recursive_mutex mutex;
  std::shared_ptr<std::function<bool(uint64_t)>> changed;
};

struct TracebackObject {
  Object header;
  Value frame;
  Value next;
  int64_t line = 0;
  // -2 means derive the instruction offset from the captured frame.  A
  // traceback created through types.TracebackType preserves the caller's
  // explicit tb_lasti value, including zero.
  int64_t lasti = -2;
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

XLANG3_HOT_INLINE bool string_object_is_ascii(const StringObject& value) {
  return (value.unicode_metadata.load(std::memory_order_relaxed) & kStringAsciiFlag) != 0;
}

XLANG3_HOT_INLINE bool string_object_is_immortal(const StringObject& value) {
  return (value.unicode_metadata.load(std::memory_order_relaxed) & kStringImmortalFlag) != 0;
}

XLANG3_HOT_INLINE void string_object_set_immortal(StringObject& value, bool immortal) {
  if (immortal) value.unicode_metadata.fetch_or(kStringImmortalFlag, std::memory_order_release);
  else value.unicode_metadata.fetch_and(~kStringImmortalFlag, std::memory_order_relaxed);
}

XLANG3_HOT_INLINE void string_object_set_ascii(StringObject& value, bool ascii) {
  const uintptr_t bit = ascii ? kStringAsciiFlag : 0;
  const uintptr_t metadata = value.unicode_metadata.load(std::memory_order_relaxed);
  if ((metadata & kStringAsciiFlag) == bit) return;
  // A changed ASCII flag belongs to unfinished construction: published bytes
  // are immutable. Unchanged published results can retain an existing index.
  // Construction needs no atomic read-modify-write on every ASCII allocation.
  value.unicode_metadata.store((metadata & ~kStringAsciiFlag) | bit, std::memory_order_relaxed);
}

XLANG3_HOT_INLINE void string_object_refresh_ascii(StringObject& value) {
  bool ascii = true;
  for (unsigned char ch : string_object_view(value)) {
    if (ch >= 0x80u) {
      ascii = false;
      break;
    }
  }
  string_object_set_ascii(value, ascii);
}

XLANG3_HOT_INLINE size_t string_view_hash(std::string_view value) {
  constexpr size_t kUncached = static_cast<size_t>(-1);
  size_t hash = std::hash<std::string_view>{}(value);
  return hash == kUncached ? kUncached - 1 : hash;
}

XLANG3_HOT_INLINE size_t string_object_hash(const StringObject& value) {
  constexpr size_t kUncached = static_cast<size_t>(-1);
  size_t hash = value.cached_hash.load(std::memory_order_relaxed);
  if (hash != kUncached) return hash;
  hash = string_view_hash(string_object_view(value));
  // Reserve the all-bits-one value as the uncached marker. This mirrors the
  // usual Python convention of remapping its hash-error sentinel.
  value.cached_hash.store(hash, std::memory_order_relaxed);
  return hash;
}

XLANG3_HOT_INLINE char* string_object_mutable_data(StringObject& value) {
  return reinterpret_cast<char*>(&value + 1);
}

inline std::string string_object_to_string(const StringObject& value) {
  return std::string(string_object_view(value));
}

Value intern_string_value(const Value& value);
Value noninterned_string_value(std::string_view value);
bool string_value_is_interned(const Value& value);
bool string_value_is_immortal_interned(const Value& value);
int64_t interned_string_count();
int64_t immortal_interned_string_count();

XLANG3_HOT_INLINE size_t utf8_codepoint_width(unsigned char ch) {
  if ((ch & 0x80u) == 0) return 1;
  if ((ch & 0xE0u) == 0xC0u) return 2;
  if ((ch & 0xF0u) == 0xE0u) return 3;
  if ((ch & 0xF8u) == 0xF0u) return 4;
  return 1;
}

XLANG3_HOT_INLINE size_t utf8_codepoint_count(std::string_view text) {
  size_t count = 0;
  for (size_t i = 0; i < text.size();) {
    const size_t width = utf8_codepoint_width(static_cast<unsigned char>(text[i]));
    i += (width <= text.size() - i) ? width : 1;
    ++count;
  }
  return count;
}

XLANG3_HOT_INLINE size_t utf8_byte_offset(std::string_view text, size_t codepoint_index) {
  size_t offset = 0;
  for (size_t i = 0; i < codepoint_index && offset < text.size(); ++i) {
    const size_t width = utf8_codepoint_width(static_cast<unsigned char>(text[offset]));
    offset += (width <= text.size() - offset) ? width : 1;
  }
  return offset;
}

XLANG3_HOT_INLINE std::string_view utf8_codepoint_at(std::string_view text, size_t codepoint_index) {
  const size_t offset = utf8_byte_offset(text, codepoint_index);
  if (offset >= text.size()) {
    return {};
  }
  const size_t width = utf8_codepoint_width(static_cast<unsigned char>(text[offset]));
  return text.substr(offset, (width <= text.size() - offset) ? width : 1);
}

size_t string_object_unicode_length(const StringObject& value);
size_t string_object_unicode_offset(const StringObject& value, size_t index);
size_t string_object_unicode_index(const StringObject& value, size_t byte_offset);

XLANG3_HOT_INLINE size_t string_object_length(const StringObject& value) {
  if (string_object_is_ascii(value)) return value.size;
  // Tiny strings do not need another allocation just to examine a few bytes.
  if (value.size <= 64) return utf8_codepoint_count(string_object_view(value));
  return string_object_unicode_length(value);
}

XLANG3_HOT_INLINE size_t string_object_byte_offset(const StringObject& value, size_t index) {
  if (string_object_is_ascii(value)) return index < value.size ? index : value.size;
  if (value.size <= 64) return utf8_byte_offset(string_object_view(value), index);
  return string_object_unicode_offset(value, index);
}

XLANG3_HOT_INLINE size_t string_object_character_index(const StringObject& value, size_t byte_offset) {
  if (byte_offset > value.size) byte_offset = value.size;
  if (string_object_is_ascii(value)) return byte_offset;
  if (value.size <= 64) return utf8_codepoint_count(string_object_view(value).substr(0, byte_offset));
  return string_object_unicode_index(value, byte_offset);
}

XLANG3_HOT_INLINE std::string_view string_object_codepoint_at(const StringObject& value, size_t index) {
  const auto text = string_object_view(value);
  const size_t offset = string_object_byte_offset(value, index);
  if (offset >= text.size()) return {};
  const size_t width = utf8_codepoint_width(static_cast<unsigned char>(text[offset]));
  return text.substr(offset, width <= text.size() - offset ? width : 1);
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
  // This construction-only pointer must not be used after publishing bytes.
  // Invalidate before native builders write; immutable readers share the hash.
  value.cached_hash.store(static_cast<size_t>(-1), std::memory_order_relaxed);
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

XLANG3_HOT_INLINE EventObject* value_as_event(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::Event) {
    return nullptr;
  }
  return reinterpret_cast<EventObject*>(value.as.obj);
}

XLANG3_HOT_INLINE TypeParamObject* value_as_type_param(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::TypeParam) {
    return nullptr;
  }
  return reinterpret_cast<TypeParamObject*>(value.as.obj);
}

XLANG3_HOT_INLINE GenericAliasObject* value_as_generic_alias(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::GenericAlias) {
    return nullptr;
  }
  return reinterpret_cast<GenericAliasObject*>(value.as.obj);
}

struct FileObject {
  Object header;
  std::recursive_mutex mutex;
  Runtime* runtime = nullptr;
  Value klass;
  bool finalizer_started = false;
  FileSystem* fs = nullptr;
  std::string path;
  std::string mode;
  std::string buffer;
  std::string encoding = "utf-8";
  std::string errors = "strict";
  std::string newline;
  bool newline_is_none = true;
  std::size_t cursor = 0;
  bool readable = false;
  bool writable = false;
  bool append = false;
  bool binary = false;
  mutable bool binary_view_pending = false;
  bool text_encoder_started = false;
  bool iteration_telling_disabled = false;
  bool text_decoded_cache = false;
  std::string text_decoded_buffer;
  int64_t buffering = -1;
  bool closed = false;
  bool devnull = false;
  bool fd_backed = false;
  int fd = -1;
  intptr_t fd_native_handle = -1;
  bool closefd = true;
  std::unordered_map<std::string, Value> attrs;
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

inline std::atomic_bool g_xlang_perf_enabled{false};
void xlang_perf_note_value_incref(ObjectKind kind);
void xlang_perf_note_value_decref(ObjectKind kind);
void release_last_reference(const Value& value);

XLANG3_HOT_INLINE void retain(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr ||
      (value.flags & kXlangValueBorrowedRefFlag) != 0) {
    return;
  }
  if (value.as.obj->kind == ObjectKind::String &&
      string_object_is_immortal(*reinterpret_cast<StringObject*>(value.as.obj))) {
    return;
  }
  if (g_xlang_perf_enabled.load(std::memory_order_relaxed)) {
    xlang_perf_note_value_incref(value.as.obj->kind);
  }
  value.as.obj->refcnt.fetch_add(1, std::memory_order_relaxed);
}

XLANG3_HOT_INLINE void release(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr ||
      (value.flags & kXlangValueBorrowedRefFlag) != 0) {
    return;
  }
  if (value.as.obj->kind == ObjectKind::String &&
      string_object_is_immortal(*reinterpret_cast<StringObject*>(value.as.obj))) {
    return;
  }
  if (g_xlang_perf_enabled.load(std::memory_order_relaxed)) {
    xlang_perf_note_value_decref(value.as.obj->kind);
  }
  // Publish this reference's writes with release ordering. Acquire only when
  // this was the final owner: non-final drops are common in VM register and
  // frame cleanup, so a fence only at zero avoids an acquire barrier for each.
  if (value.as.obj->refcnt.fetch_sub(1, std::memory_order_release) == 1) {
    std::atomic_thread_fence(std::memory_order_acquire);
    release_last_reference(value);
  }
}

XLANG3_HOT_INLINE Value::Value(const Value& other)
    : tag(other.tag), flags(other.flags & ~kXlangValueBorrowedRefFlag), as(other.as) {
  retain(*this);
}

XLANG3_HOT_INLINE Value::Value(Value&& other) noexcept
    : tag(other.tag), flags(other.flags), as(other.as) {
  other.tag = ValueTag::Invalid;
  other.flags = 0;
  other.as.obj = nullptr;
}

XLANG3_HOT_INLINE Value& Value::operator=(const Value& other) {
  if (this == &other) return *this;
  if (tag == ValueTag::Object && other.tag == ValueTag::Object &&
      as.obj == other.as.obj &&
      (flags & kXlangValueBorrowedRefFlag) == 0) {
    // Owning self-assignments are common when VM temporaries and container
    // slots already hold the same object. Copy assignment would otherwise
    // perform a redundant atomic retain/release pair. A borrowed destination
    // must still take the general path so it acquires its own reference.
    flags = other.flags & ~kXlangValueBorrowedRefFlag;
    return *this;
  }
  // The source can borrow the same object currently owned by this slot.
  // Acquire its reference before releasing the destination's reference.
  Value retained(other);
  return *this = std::move(retained);
}

XLANG3_HOT_INLINE Value& Value::operator=(Value&& other) noexcept {
  if (this == &other) return *this;
  release(*this);
  tag = other.tag;
  flags = other.flags;
  as = other.as;
  other.tag = ValueTag::Invalid;
  other.flags = 0;
  other.as.obj = nullptr;
  return *this;
}

XLANG3_HOT_INLINE Value::~Value() {
  release(*this);
}

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
    // Re-loading a stable cached object into its existing register must not
    // churn the atomic refcount: either slot's current ownership is already
    // correct when both values name the same object. Keep scalar assignments
    // on the original branch path by checking identity only for object copies.
    if (out.tag == ValueTag::Object && out.as.obj == value.as.obj) {
      return;
    }
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
  out.flags = next_float_identity();
  out.as.f64 = value;
}

std::string value_to_string(const Value& value);
std::string value_to_repr(const Value& value);
bool value_truthy(const Value& value);
bool value_finalize_temporary_instance(Runtime& runtime, const Value& value);
// Shared version-cached finalizer predicate used by release and guarded execution.
bool class_has_release_finalizer(const Value& klass_value);
const char* value_binary_type_name(const Value& value);

bool value_add(const Value& lhs, const Value& rhs, Value& out, std::string& error);
bool value_sub(const Value& lhs, const Value& rhs, Value& out, std::string& error);
bool value_mul(const Value& lhs, const Value& rhs, Value& out, std::string& error);
bool value_matmul(const Value& lhs, const Value& rhs, Value& out, std::string& error);
bool value_div(const Value& lhs, const Value& rhs, Value& out, std::string& error);
bool value_floor_div(const Value& lhs, const Value& rhs, Value& out, std::string& error);
bool value_mod(const Value& lhs, const Value& rhs, Value& out, std::string& error);
bool value_mod_runtime(Runtime& runtime, const Value& lhs, const Value& rhs, Value& out, std::string& error);
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
bool event_subscribe(Value event, Value callable, uint64_t& cookie, std::string& error);
bool event_unsubscribe(Value event, uint64_t cookie, std::string& error);
bool event_fire(Runtime& runtime, Value event, const Value* args, uint32_t argc, Value& out, std::string& error);
bool event_fire_kw(Runtime& runtime, Value event, const Value* args, uint32_t argc,
    const std::vector<std::pair<std::string, Value>>& kwargs, Value& out, std::string& error);

} // namespace xlang3
