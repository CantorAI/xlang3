/* Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
   Licensed under the Apache License, Version 2.0. */

#include "ctypes_ffi.h"
#include "xlang3/functional_iterators.h"
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"

#include <cstdint>
#include <cstring>
#include <algorithm>
#include <memory>
#include <limits>
#include <string>
#include <vector>

#ifdef XLANG_CTYPES_HAS_LIBFFI
extern "C" {
#include "ffi.h"
}
#endif

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace xlang3 {
namespace {


bool attribute(const Value& object, const char* name, Value& result) {
  std::string ignored;
  return object_get_attr(object, name, result, ignored);
}

bool fail(Runtime& runtime, const char* exception, std::string message,
          std::string& error) {
  error = std::move(message);
  runtime.raise_class_error(exception, error);
  return false;
}

bool integer(const Value& value, int64_t& output) {
  if (value.tag == ValueTag::Int64) {
    output = value.as.i64;
    return true;
  }
  if (value.tag == ValueTag::Bool) {
    output = value.as.b ? 1 : 0;
    return true;
  }
  return false;
}

bool type_code(const Value& type, char& code) {
  Value marker;
  if (!attribute(type, "_type_", marker)) return false;
  const auto* text = value_as_string(marker);
  if (text == nullptr) return false;
  const auto name = string_object_view(*text);
  if (name.size() != 1) return false;
  code = name[0];
  return true;
}

bool utf8_to_wide(std::string_view text, std::wstring& output) {
#if defined(_WIN32)
  if (text.empty()) {
    output.clear();
    return true;
  }
  const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
      text.data(), static_cast<int>(text.size()), nullptr, 0);
  if (count <= 0) return false;
  output.resize(static_cast<size_t>(count));
  return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
      text.data(), static_cast<int>(text.size()), output.data(), count) == count;
#else
  (void)text;
  (void)output;
  return false;
#endif
}

bool wide_to_utf8(const wchar_t* text, std::string& output) {
#if defined(_WIN32)
  if (text == nullptr) return false;
  const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
      text, -1, nullptr, 0, nullptr, nullptr);
  if (count <= 0) return false;
  output.resize(static_cast<size_t>(count));
  if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
      text, -1, output.data(), count, nullptr, nullptr) != count) return false;
  output.pop_back();
  return true;
#else
  (void)text;
  (void)output;
  return false;
#endif
}

size_t scalar_size(char code) {
  switch (code) {
    case '?': case 'b': case 'B': case 'c': return 1;
    case 'h': case 'H': return 2;
    case 'u': return sizeof(wchar_t);
    case 'i': case 'I': case 'l': case 'L': case 'f': return 4;
    case 'q': case 'Q': case 'd': case 'P': case 'z': case 'Z':
    case 'O': return 8;
    case 'g': case 'F': return 8;
    case 'D': case 'G': return 16;
    case 'v': return 2;
    default: return 0;
  }
}

size_t aligned(size_t value, size_t alignment) {
  return (value + alignment - 1) & ~(alignment - 1);
}

struct Shape {
  enum class Kind { Scalar, Pointer, Array, Structure } kind = Kind::Scalar;
  char code = 0;
  size_t size = 0;
  size_t alignment = 1;
  Value element;
  size_t length = 0;
};

bool shape_of(const Value& type, Shape& output, unsigned depth = 0) {
  if (depth > 32) return false;
  Value fields;
  if (attribute(type, "_fields_", fields)) {
    const auto* list = value_as_list(fields);
    const auto* tuple = value_as_tuple(fields);
    if (list == nullptr && tuple == nullptr) return false;
    const auto& definitions = list != nullptr ? list->items : tuple->items;
    size_t offset = 0;
    size_t max_alignment = 1;
    Value packing;
    size_t pack = 0;
    if (attribute(type, "_pack_", packing)) {
      int64_t requested = 0;
      if (!integer(packing, requested) || requested < 0) return false;
      pack = static_cast<size_t>(requested);
    }
    const bool is_union = value_as_class(type) != nullptr &&
        class_has_builtin_base_name(value_as_class(type), "Union");
    for (const Value& entry : definitions) {
      const auto* pair = value_as_tuple(entry);
      if (pair == nullptr || pair->items.size() < 2) return false;
      Shape field;
      if (!shape_of(pair->items[1], field, depth + 1)) return false;
      const size_t field_alignment = pack == 0 ? field.alignment : std::min(field.alignment, pack);
      if (is_union) {
        offset = std::max(offset, field.size);
      } else {
        offset = aligned(offset, field_alignment);
        if (field.size > std::numeric_limits<size_t>::max() - offset) return false;
        offset += field.size;
      }
      max_alignment = std::max(max_alignment, field_alignment);
    }
    output.kind = Shape::Kind::Structure;
    output.size = aligned(offset, max_alignment);
    output.alignment = max_alignment;
    return true;
  }
  Value element;
  if (!attribute(type, "_type_", element)) return false;
  if (const auto* code_string = value_as_string(element)) {
    const auto code = string_object_view(*code_string);
    if (code.size() != 1) return false;
    output.kind = Shape::Kind::Scalar;
    output.code = code[0];
    output.size = scalar_size(output.code);
    output.alignment = std::min(output.size, sizeof(void*));
    return output.size != 0;
  }
  Value length;
  if (attribute(type, "_length_", length)) {
    int64_t count = 0;
    if (!integer(length, count) || count < 0) return false;
    Shape item;
    if (!shape_of(element, item, depth + 1)) return false;
    output.kind = Shape::Kind::Array;
    output.element = element;
    output.length = static_cast<size_t>(count);
    if (item.size != 0 && output.length > std::numeric_limits<size_t>::max() / item.size) return false;
    output.size = item.size * output.length;
    output.alignment = item.alignment;
    return true;
  }
  output.kind = Shape::Kind::Pointer;
  output.element = element;
  output.size = sizeof(void*);
  output.alignment = alignof(void*);
  return true;
}

struct CDataStorage {
  size_t size;
  std::unique_ptr<uint64_t[]> words;
  std::vector<std::unique_ptr<std::wstring>> wide_strings;
  std::vector<std::unique_ptr<std::string>> narrow_strings;
  std::vector<Value> keepalive;
  explicit CDataStorage(size_t count)
      : size(count), words(new uint64_t[(count + 7) / 8]()) {}
  void* data() { return words.get(); }
};

constexpr const char* kStorageType = "xlang3.ctypes.native_storage";
void cleanup_storage(void* pointer) { delete static_cast<CDataStorage*>(pointer); }

CDataStorage* storage_for(const Value& object, size_t size,
                          std::string& error) {
  auto* existing = static_cast<CDataStorage*>(
      instance_get_native_data(object, kStorageType));
  if (existing != nullptr) {
    if (existing->size != size) error = "ctypes storage size changed";
    return existing->size == size ? existing : nullptr;
  }
  auto storage = std::make_unique<CDataStorage>(size);
  if (!instance_set_native_data(object, kStorageType, storage.get(),
                                cleanup_storage, error)) return nullptr;
  return storage.release();
}

bool store_scalar(const Value& object, const Shape& shape,
                  void* destination, CDataStorage& owner, std::string& error) {
  Value source = object;
  if (value_as_instance(object) != nullptr) {
    Value contained;
    if (attribute(object, "value", contained)) source = std::move(contained);
  }
  if (source.tag == ValueTag::None) return true;
  if (shape.code == 'Z' || shape.code == 'z') {
    uintptr_t address = 0;
    int64_t numeric_address = 0;
    if (integer(source, numeric_address)) {
      address = static_cast<uintptr_t>(numeric_address);
      std::memcpy(destination, &address, sizeof(address));
      return true;
    }
    if (shape.code == 'Z') {
      const auto* text = value_as_string(source);
      if (text == nullptr) { error = "expected Unicode string for c_wchar_p field"; return false; }
      auto held = std::make_unique<std::wstring>();
      if (!utf8_to_wide(string_object_view(*text), *held)) {
        error = "invalid Unicode string for c_wchar_p field";
        return false;
      }
      address = reinterpret_cast<uintptr_t>(held->c_str());
      owner.wide_strings.push_back(std::move(held));
    } else {
      const auto* bytes = value_as_bytes(source);
      if (bytes == nullptr) { error = "expected bytes for c_char_p field"; return false; }
      auto held = std::make_unique<std::string>(bytes_object_to_string(*bytes));
      address = reinterpret_cast<uintptr_t>(held->c_str());
      owner.narrow_strings.push_back(std::move(held));
    }
    std::memcpy(destination, &address, sizeof(address));
    return true;
  }
  int64_t number = 0;
  if (!integer(source, number)) { error = "expected integer for ctypes field"; return false; }
  std::memcpy(destination, &number, shape.size);
  return true;
}

bool store_structure(const Value& object, const Value& type,
                     void* destination, CDataStorage& owner, std::string& error);

bool store_array(const Value& object, const Shape& shape,
                 void* destination, CDataStorage& owner, std::string& error) {
  if (shape.kind != Shape::Kind::Array) return false;
  Shape element_shape;
  if (!shape_of(shape.element, element_shape)) return false;
  Value items_value;
  if (!attribute(object, "_array_items_", items_value)) return false;
  const auto* items = value_as_list(items_value);
  if (items == nullptr || items->items.size() != shape.length) return false;
  for (size_t index = 0; index < shape.length; ++index) {
    void* slot = static_cast<uint8_t*>(destination) + index * element_shape.size;
    const Value& item = items->items[index];
    if (element_shape.kind == Shape::Kind::Scalar) {
      if (!store_scalar(item, element_shape, slot, owner, error)) return false;
    } else if (element_shape.kind == Shape::Kind::Structure) {
      if (!store_structure(item, shape.element, slot, owner, error)) return false;
    } else if (element_shape.kind == Shape::Kind::Array) {
      if (!store_array(item, element_shape, slot, owner, error)) return false;
    } else {
      Value address_value;
      int64_t raw_address = 0;
      if (attribute(item, "_address_", address_value) && integer(address_value, raw_address)) {
        uintptr_t address = static_cast<uintptr_t>(raw_address);
        std::memcpy(slot, &address, sizeof(address));
      }
      owner.keepalive.push_back(item);
    }
  }
  return true;
}

bool store_structure(const Value& object, const Value& type,
                     void* destination, CDataStorage& owner, std::string& error) {
  Value fields;
  if (!attribute(type, "_fields_", fields)) return false;
  const auto* list = value_as_list(fields);
  const auto* tuple = value_as_tuple(fields);
  if (list == nullptr && tuple == nullptr) return false;
  const auto& definitions = list != nullptr ? list->items : tuple->items;
  size_t offset = 0;
  Value packing;
  int64_t requested_pack = 0;
  if (attribute(type, "_pack_", packing) &&
      (!integer(packing, requested_pack) || requested_pack < 0)) return false;
  const bool is_union = value_as_class(type) != nullptr &&
      class_has_builtin_base_name(value_as_class(type), "Union");
  for (const Value& entry : definitions) {
    const auto* pair = value_as_tuple(entry);
    if (pair == nullptr || pair->items.size() < 2) return false;
    const auto* name = value_as_string(pair->items[0]);
    Shape field;
    if (name == nullptr || !shape_of(pair->items[1], field)) return false;
    const size_t field_alignment = requested_pack == 0 ? field.alignment :
        std::min(field.alignment, static_cast<size_t>(requested_pack));
    if (!is_union) offset = aligned(offset, field_alignment);
    Value field_value;
    if (attribute(object, string_object_to_string(*name).c_str(), field_value)) {
      auto* field_memory = static_cast<uint8_t*>(destination) + offset;
      if (field.kind == Shape::Kind::Scalar) {
        if (!store_scalar(field_value, field, field_memory, owner, error)) return false;
      } else if (field.kind == Shape::Kind::Structure) {
        if (!store_structure(field_value, pair->items[1], field_memory, owner, error)) return false;
      } else if (field.kind == Shape::Kind::Array) {
        if (!store_array(field_value, field, field_memory, owner, error)) return false;
      } else if (field.kind == Shape::Kind::Pointer) {
        Value address_value;
        int64_t raw_address = 0;
        uintptr_t address = 0;
        if (attribute(field_value, "_address_", address_value) &&
            integer(address_value, raw_address)) {
          address = static_cast<uintptr_t>(raw_address);
        } else {
          if (value_as_instance(field_value) != nullptr) {
            Shape actual_shape;
            if (shape_of(value_as_instance(field_value)->klass, actual_shape) &&
                actual_shape.kind == Shape::Kind::Array) {
              auto* storage = storage_for(field_value, actual_shape.size, error);
              if (storage == nullptr ||
                  !store_array(field_value, actual_shape, storage->data(), *storage, error)) return false;
              address = reinterpret_cast<uintptr_t>(storage->data());
            }
          }
          Value referent;
          if (address == 0 && attribute(field_value, "value", referent) &&
              value_as_instance(referent) != nullptr) {
            Shape referent_shape;
            if (!shape_of(field.element, referent_shape)) return false;
            auto* storage = storage_for(referent, referent_shape.size, error);
            if (storage == nullptr) return false;
            if (referent_shape.kind == Shape::Kind::Structure &&
                !store_structure(referent, field.element, storage->data(), *storage, error)) return false;
            if (referent_shape.kind == Shape::Kind::Scalar &&
                !store_scalar(referent, referent_shape, storage->data(), *storage, error)) return false;
            address = reinterpret_cast<uintptr_t>(storage->data());
          }
        }
        std::memcpy(field_memory, &address, sizeof(address));
        owner.keepalive.push_back(field_value);
      }
    }
    if (!is_union) offset += field.size;
  }
  return true;
}

bool readback(Value object, const Value& type, const Shape& shape,
              const void* memory, std::string& error) {
  if (shape.kind == Shape::Kind::Array) {
    Shape element;
    if (!shape_of(shape.element, element)) return false;
    if (element.kind == Shape::Kind::Scalar && element.code == 'u') {
      const auto* characters = static_cast<const wchar_t*>(memory);
      size_t length = 0;
      while (length < shape.length && characters[length] != 0) ++length;
      std::wstring terminated(characters, characters + length);
      std::string utf8;
      if (!wide_to_utf8(terminated.c_str(), utf8)) return false;
      return object_set_attr(object, "value", Value::string(std::move(utf8)), error);
    }
    if (element.kind == Shape::Kind::Scalar && element.code == 'c') {
      const auto* characters = static_cast<const char*>(memory);
      size_t length = 0;
      while (length < shape.length && characters[length] != 0) ++length;
      return object_set_attr(object, "value", Value::bytes(std::string_view(characters, length)), error);
    }
    return true;
  }
  if (shape.kind == Shape::Kind::Pointer) {
    uintptr_t address = 0;
    std::memcpy(&address, memory, sizeof(address));
    return object_set_attr(object, "_address_", Value::int64(static_cast<int64_t>(address)), error);
  }
  if (shape.kind == Shape::Kind::Scalar && shape.code == 'Z') {
    const wchar_t* pointer = nullptr;
    std::memcpy(&pointer, memory, sizeof(pointer));
    Value decoded = Value::none();
    if (pointer != nullptr) {
      std::string utf8;
      if (!wide_to_utf8(pointer, utf8)) {
        error = "invalid UTF-16 output from native function";
        return false;
      }
      decoded = Value::string(std::move(utf8));
    }
    return object_set_attr(object, "value", decoded, error);
  }
  if (shape.kind != Shape::Kind::Structure) return true;
  Value fields;
  if (!attribute(type, "_fields_", fields)) return false;
  const auto* list = value_as_list(fields);
  const auto* tuple = value_as_tuple(fields);
  if (list == nullptr && tuple == nullptr) return false;
  const auto& definitions = list != nullptr ? list->items : tuple->items;
  size_t offset = 0;
  for (const Value& entry : definitions) {
    const auto* pair = value_as_tuple(entry);
    if (pair == nullptr || pair->items.size() < 2) return false;
    const auto* name = value_as_string(pair->items[0]);
    Shape field;
    if (name == nullptr || !shape_of(pair->items[1], field)) return false;
    offset = aligned(offset, field.alignment);
    if (field.kind == Shape::Kind::Scalar && field.size <= 8) {
      uint64_t bits = 0;
      std::memcpy(&bits, static_cast<const uint8_t*>(memory) + offset,
                  field.size);
      if (!object_set_attr(object, string_object_to_string(*name),
                           Value::int64(static_cast<int64_t>(bits)), error))
        return false;
    }
    offset += field.size;
  }
  return true;
}

struct ForeignArgument {
  uint64_t storage = 0;
  std::wstring wide;
  std::string narrow;
  Value pointee_object;
  Value pointee_type;
  Shape pointee_shape;
  CDataStorage* pointee_storage = nullptr;
#ifdef XLANG_CTYPES_HAS_LIBFFI
  ffi_type* abi = &ffi_type_void;
#endif
};

#ifdef XLANG_CTYPES_HAS_LIBFFI
ffi_type* abi_type(char code) {
  switch (code) {
    case '?': case 'b': return &ffi_type_sint8;
    case 'B': case 'c': return &ffi_type_uint8;
    case 'h': return &ffi_type_sint16;
    case 'H': case 'u': return &ffi_type_uint16;
    case 'i': case 'l': return &ffi_type_sint32;
    case 'I': case 'L': return &ffi_type_uint32;
    case 'q': return &ffi_type_sint64;
    case 'Q': return &ffi_type_uint64;
    case 'f': return &ffi_type_float;
    case 'd': return &ffi_type_double;
    case 'P': case 'z': case 'Z': case 'O': return &ffi_type_pointer;
    default: return nullptr;
  }
}

bool marshal_pointer(Runtime& runtime, const Value& argument,
                     const Value& pointer_type, ForeignArgument& output,
                     std::string& error) {
  Shape pointer_shape;
  if (!shape_of(pointer_type, pointer_shape) ||
      pointer_shape.kind != Shape::Kind::Pointer)
    return fail(runtime, "TypeError", "expected ctypes pointer type", error);
  output.abi = &ffi_type_pointer;
  if (argument.tag == ValueTag::None) return true;
  Value address;
  int64_t raw_address = 0;
  if (attribute(argument, "_address_", address) && integer(address, raw_address)) {
    output.storage = static_cast<uint64_t>(raw_address);
    return true;
  }
  Value referent;
  const Value* pointee = &argument;
  if (attribute(argument, "value", referent) && value_as_instance(referent) != nullptr) {
    pointee = &referent;
  }
  Shape pointee_shape;
  if (!shape_of(pointer_shape.element, pointee_shape))
    return fail(runtime, "NotImplementedError", "unsupported ctypes pointee type", error);
  const bool existing = instance_get_native_data(*pointee, kStorageType) != nullptr;
  CDataStorage* storage = storage_for(*pointee, pointee_shape.size, error);
  if (storage == nullptr)
    return fail(runtime, "TypeError", error.empty() ?
                "expected ctypes instance for pointer argument" : error, error);
  if (!existing) {
    if (pointee_shape.kind == Shape::Kind::Structure &&
        !store_structure(*pointee, pointer_shape.element, storage->data(), *storage, error))
      return fail(runtime, "TypeError", "cannot marshal ctypes structure", error);
    if (pointee_shape.kind == Shape::Kind::Scalar)
      if (!store_scalar(*pointee, pointee_shape, storage->data(), *storage, error))
        return fail(runtime, "TypeError", error, error);
  }
  output.storage = reinterpret_cast<uintptr_t>(storage->data());
  output.pointee_object = *pointee;
  output.pointee_type = pointer_shape.element;
  output.pointee_shape = pointee_shape;
  output.pointee_storage = storage;
  return true;
}

bool marshal_argument(Runtime& runtime, const Value& argument, char code,
                      ForeignArgument& output, std::string& error) {
  output.abi = abi_type(code);
  if (output.abi == nullptr)
    return fail(runtime, "NotImplementedError",
                "unsupported ctypes argument type code", error);
  if (code == 'Z' || code == 'z') {
    if (value_as_instance(argument) != nullptr) {
      Shape array_shape;
      const Value array_type = value_as_instance(argument)->klass;
      if (shape_of(array_type, array_shape) && array_shape.kind == Shape::Kind::Array) {
        auto* storage = storage_for(argument, array_shape.size, error);
        if (storage == nullptr ||
            !store_array(argument, array_shape, storage->data(), *storage, error))
          return fail(runtime, "TypeError", "cannot marshal ctypes array", error);
        output.storage = reinterpret_cast<uintptr_t>(storage->data());
        output.pointee_object = argument;
        output.pointee_type = array_type;
        output.pointee_shape = array_shape;
        output.pointee_storage = storage;
        return true;
      }
    }
    Value source = argument;
    if (value_as_instance(argument) != nullptr) {
      Value contained;
      if (attribute(argument, "value", contained)) source = std::move(contained);
    }
    if (source.tag == ValueTag::None) return true;
    if (code == 'Z') {
      const auto* text = value_as_string(source);
      if (text == nullptr || !utf8_to_wide(string_object_view(*text), output.wide))
        return fail(runtime, "TypeError", "expected Unicode string for c_wchar_p", error);
      output.storage = reinterpret_cast<uintptr_t>(output.wide.c_str());
    } else {
      const auto* bytes = value_as_bytes(source);
      if (bytes == nullptr)
        return fail(runtime, "TypeError", "expected bytes for c_char_p", error);
      output.narrow = bytes_object_to_string(*bytes);
      output.storage = reinterpret_cast<uintptr_t>(output.narrow.c_str());
    }
    return true;
  }
  if (code == 'P' && value_as_instance(argument) != nullptr) {
    auto* storage = static_cast<CDataStorage*>(
        instance_get_native_data(argument, kStorageType));
    if (storage != nullptr && storage->size >= sizeof(void*)) {
      std::memcpy(&output.storage, storage->data(), sizeof(void*));
      return true;
    }
  }
  Value source = argument;
  if (value_as_instance(argument) != nullptr) {
    Value contained;
    if (attribute(argument, "value", contained)) source = std::move(contained);
  }
  int64_t number = 0;
  if (source.tag == ValueTag::None && code == 'P') {
    output.storage = 0;
    return true;
  }
  if (code == 'f' || code == 'd') {
    double floating = 0;
    if (source.tag == ValueTag::Double) floating = source.as.f64;
    else if (integer(source, number)) floating = static_cast<double>(number);
    else return fail(runtime, "TypeError", "expected number for ctypes argument", error);
    if (code == 'f') {
      const float narrowed = static_cast<float>(floating);
      std::memcpy(&output.storage, &narrowed, sizeof(narrowed));
    } else {
      std::memcpy(&output.storage, &floating, sizeof(floating));
    }
    return true;
  }
  if (!integer(source, number))
    return fail(runtime, "TypeError", "expected integer for ctypes argument", error);
  output.storage = static_cast<uint64_t>(number);
  return true;
}

Value decode_result(char code, uint64_t storage) {
  switch (code) {
    case '?': return Value::boolean((storage & 0xffu) != 0);
    case 'b': return Value::int64(static_cast<int8_t>(storage));
    case 'B': case 'c': return Value::int64(static_cast<uint8_t>(storage));
    case 'h': return Value::int64(static_cast<int16_t>(storage));
    case 'H': case 'u': return Value::int64(static_cast<uint16_t>(storage));
    case 'i': case 'l': return Value::int64(static_cast<int32_t>(storage));
    case 'I': case 'L': return Value::int64(static_cast<uint32_t>(storage));
    case 'q': return Value::int64(static_cast<int64_t>(storage));
    case 'Q': case 'P': case 'z': case 'Z': case 'O':
      return Value::int64(static_cast<int64_t>(storage));
    case 'f': {
      float number = 0;
      std::memcpy(&number, &storage, sizeof(number));
      return Value::number(number);
    }
    case 'd': {
      double number = 0;
      std::memcpy(&number, &storage, sizeof(number));
      return Value::number(number);
    }
    default: return Value::none();
  }
}
#endif

} // namespace

bool ctypes_type_layout(const Value& type, size_t& size, size_t& alignment) {
  Shape shape;
  if (!shape_of(type, shape)) return false;
  size = shape.size;
  alignment = shape.alignment;
  return true;
}

bool ctypes_pointer_read(Runtime& runtime, const Value& pointer, int64_t index,
                         Value& out, std::string& error) {
  const auto* instance = value_as_instance(pointer);
  if (instance == nullptr) return fail(runtime, "TypeError", "expected ctypes pointer", error);
  Value element;
  if (!attribute(instance->klass, "_type_", element))
    return fail(runtime, "TypeError", "pointer has no element type", error);

  Value address;
  int64_t raw_address = 0;
  if (!attribute(pointer, "_address_", address) || !integer(address, raw_address)) {
    Value referent;
    if (index == 0 && attribute(pointer, "value", referent) &&
        value_as_instance(referent) != nullptr) {
      out = std::move(referent);
      return true;
    }
    return fail(runtime, "ValueError", "NULL pointer access", error);
  }
  if (raw_address == 0) return fail(runtime, "ValueError", "NULL pointer access", error);
  Shape shape;
  if (!shape_of(element, shape) || shape.size > sizeof(uint64_t))
    return fail(runtime, "NotImplementedError", "unsupported ctypes pointer element", error);
  if (shape.size != 0 && (index > std::numeric_limits<int64_t>::max() / static_cast<int64_t>(shape.size) ||
                          index < std::numeric_limits<int64_t>::min() / static_cast<int64_t>(shape.size)))
    return fail(runtime, "OverflowError", "pointer index is too large", error);
  const auto byte_offset = index * static_cast<int64_t>(shape.size);
  const auto* source = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(raw_address) + byte_offset);
  if (shape.kind == Shape::Kind::Pointer) {
    uintptr_t pointee_address = 0;
    std::memcpy(&pointee_address, source, sizeof(pointee_address));
    Value pointee;
    if (!runtime_call_callable(runtime, element, nullptr, 0, pointee, error)) return false;
    if (!object_set_attr(pointee, "_address_", Value::int64(static_cast<int64_t>(pointee_address)), error))
      return false;
    out = std::move(pointee);
    return true;
  }
  if (shape.kind != Shape::Kind::Scalar)
    return fail(runtime, "NotImplementedError", "unsupported ctypes pointer element", error);
  Value decoded;
  if (shape.code == 'u') {
    wchar_t character[2] = {0, 0};
    std::memcpy(&character[0], source, sizeof(wchar_t));
    std::string utf8;
    if (!wide_to_utf8(character, utf8))
      return fail(runtime, "UnicodeError", "invalid native wide character", error);
    decoded = Value::string(std::move(utf8));
  } else if (shape.code == 'c') {
    decoded = Value::bytes(std::string_view(reinterpret_cast<const char*>(source), 1));
  } else {
    uint64_t bits = 0;
    std::memcpy(&bits, source, shape.size);
    decoded = decode_result(shape.code, bits);
  }
  if (!runtime_call_callable(runtime, element, &decoded, 1, out, error)) return false;
  return true;
}

bool ctypes_foreign_load_library(Runtime& runtime, const Value* args,
                                 uint32_t argc, Value& out,
                                 std::string& error) {
  if (argc < 1 || argc > 2)
    return fail(runtime, "TypeError", "LoadLibrary() requires a library name", error);
#if defined(_WIN32)
  HMODULE handle = nullptr;
  if (args[0].tag == ValueTag::None) {
    handle = GetModuleHandleW(nullptr);
  } else {
    const auto* name = value_as_string(args[0]);
    std::wstring wide;
    if (name == nullptr || !utf8_to_wide(string_object_view(*name), wide))
      return fail(runtime, "TypeError", "invalid library name", error);
    int64_t flags = 0;
    if (argc == 2 && !integer(args[1], flags))
      return fail(runtime, "TypeError", "invalid library load flags", error);
    handle = LoadLibraryExW(wide.c_str(), nullptr, static_cast<DWORD>(flags));
  }
  if (handle == nullptr)
    return fail(runtime, "OSError", "cannot load native library (Windows error " +
                std::to_string(GetLastError()) + ")", error);
  out = Value::int64(reinterpret_cast<int64_t>(handle));
  return true;
#else
  (void)out;
  return fail(runtime, "NotImplementedError", "native library loading is unavailable", error);
#endif
}

bool ctypes_foreign_call(Runtime& runtime, const Value* args,
                         uint32_t argc, Value& out,
                         std::string& error) {
  if (argc < 1)
    return fail(runtime, "TypeError", "invalid C function pointer", error);
#if !defined(XLANG_CTYPES_HAS_LIBFFI)
  (void)out;
  return fail(runtime, "NotImplementedError", "libffi is unavailable for ctypes", error);
#else
  Value target;
  if (!attribute(args[0], "_target_", target))
    return fail(runtime, "TypeError", "C function has no target", error);
  if (target.tag == ValueTag::Int64 && target.as.i64 == 5) {
    if (argc != 4 || value_as_instance(args[1]) == nullptr ||
        value_as_class(args[3]) == nullptr)
      return fail(runtime, "TypeError", "cast() requires a ctypes object and type", error);
    const Value source_type = value_as_instance(args[1])->klass;
    Shape source_shape;
    if (!shape_of(source_type, source_shape))
      return fail(runtime, "TypeError", "cast() requires a ctypes object", error);
    uintptr_t address = 0;
    if (source_shape.kind == Shape::Kind::Pointer) {
      ForeignArgument source;
      if (!marshal_pointer(runtime, args[1], source_type, source, error)) return false;
      address = static_cast<uintptr_t>(source.storage);
    } else {
      auto* storage = storage_for(args[1], source_shape.size, error);
      if (storage == nullptr) return false;
      address = reinterpret_cast<uintptr_t>(storage->data());
    }
    Shape destination_shape;
    if (!shape_of(args[3], destination_shape))
      return fail(runtime, "TypeError", "cast() requires a ctypes type", error);
    if (destination_shape.kind == Shape::Kind::Pointer) {
      if (!runtime_call_callable(runtime, args[3], nullptr, 0, out, error)) return false;
      if (!object_set_attr(out, "_address_", Value::int64(static_cast<int64_t>(address)), error))
        return false;
    } else if (destination_shape.kind == Shape::Kind::Scalar &&
               destination_shape.code == 'P') {
      Value argument = Value::int64(static_cast<int64_t>(address));
      if (!runtime_call_callable(runtime, args[3], &argument, 1, out, error)) return false;
    } else {
      return fail(runtime, "TypeError", "cast() target must be a pointer type", error);
    }
    return object_set_attr(out, "_keepalive_", args[1], error);
  }
  const auto* pair = value_as_tuple(target);
  if (pair == nullptr || pair->items.size() != 2)
    return fail(runtime, "NotImplementedError", "unsupported C function target", error);
  const auto* name = value_as_string(pair->items[0]);
  Value handle_value;
  int64_t handle_number = 0;
  if (name == nullptr || !attribute(pair->items[1], "_handle", handle_value) ||
      !integer(handle_value, handle_number))
    return fail(runtime, "TypeError", "invalid native library handle", error);
#if defined(_WIN32)
  auto* function = GetProcAddress(reinterpret_cast<HMODULE>(handle_number),
                                  string_object_to_string(*name).c_str());
#else
  auto* function = dlsym(reinterpret_cast<void*>(handle_number),
                         string_object_to_string(*name).c_str());
#endif
  if (function == nullptr)
    return fail(runtime, "AttributeError", "native function was not found", error);

  Value argument_types;
  const bool has_types = attribute(args[0], "argtypes", argument_types) &&
                         argument_types.tag != ValueTag::None;
  const auto* list_types = has_types ? value_as_list(argument_types) : nullptr;
  const auto* tuple_types = has_types ? value_as_tuple(argument_types) : nullptr;
  const size_t type_count = list_types != nullptr ? list_types->items.size() :
                            tuple_types != nullptr ? tuple_types->items.size() : 0;
  if (has_types && ((list_types == nullptr && tuple_types == nullptr) ||
                    type_count != argc - 1))
    return fail(runtime, "TypeError", "ctypes argument count does not match argtypes", error);
  std::vector<ForeignArgument> marshalled(argc - 1);
  std::vector<ffi_type*> abi_types(argc - 1);
  std::vector<void*> abi_values(argc - 1);
  for (uint32_t index = 1; index < argc; ++index) {
    char code = 'i';
    const Value& argument_type = list_types != nullptr ? list_types->items[index - 1] :
                                 tuple_types != nullptr ? tuple_types->items[index - 1] : args[index];
    const bool simple = !has_types || type_code(argument_type, code);
    if (simple) {
      if (!marshal_argument(runtime, args[index], code,
                            marshalled[index - 1], error)) return false;
    } else if (!marshal_pointer(runtime, args[index],
                                argument_type,
                                marshalled[index - 1], error)) {
      return false;
    }
    abi_types[index - 1] = marshalled[index - 1].abi;
    abi_values[index - 1] = &marshalled[index - 1].storage;
  }

  Value restype;
  char return_code = 'i';
  bool has_restype = attribute(args[0], "restype", restype);
  if (!has_restype) has_restype = attribute(args[0], "_restype_", restype);
  ffi_type* return_abi = &ffi_type_sint32;
  bool pointer_return = false;
  if (has_restype && restype.tag == ValueTag::None) {
    return_abi = &ffi_type_void;
    return_code = 0;
  } else if (has_restype) {
    if (type_code(restype, return_code)) {
      if ((return_abi = abi_type(return_code)) == nullptr)
        return fail(runtime, "NotImplementedError", "unsupported ctypes return type", error);
    } else {
      Shape result_shape;
      if (!shape_of(restype, result_shape) || result_shape.kind != Shape::Kind::Pointer)
        return fail(runtime, "NotImplementedError", "unsupported ctypes return type", error);
      return_abi = &ffi_type_pointer;
      return_code = 'P';
      pointer_return = true;
    }
  }
  ffi_cif cif{};
  if (ffi_prep_cif(&cif, FFI_DEFAULT_ABI, argc - 1, return_abi,
                   abi_types.data()) != FFI_OK)
    return fail(runtime, "RuntimeError", "libffi could not prepare ctypes call", error);
  uint64_t returned = 0;
  ffi_call(&cif, reinterpret_cast<void (*)()>(function),
           return_code == 0 ? nullptr : &returned, abi_values.data());
  for (auto& argument : marshalled) {
    if (argument.pointee_storage != nullptr &&
        !readback(argument.pointee_object, argument.pointee_type,
                  argument.pointee_shape,
                  argument.pointee_storage->data(), error))
      return fail(runtime, "ValueError", "cannot decode ctypes out parameter", error);
  }
  if (pointer_return) {
    Value pointer;
    if (!runtime_call_callable(runtime, restype, nullptr, 0, pointer, error)) return false;
    if (!object_set_attr(pointer, "_address_", Value::int64(static_cast<int64_t>(returned)), error))
      return false;
    out = std::move(pointer);
  } else {
    out = return_code == 0 ? Value::none() : decode_result(return_code, returned);
  }
  Value errcheck;
  if (attribute(args[0], "errcheck", errcheck) && errcheck.tag != ValueTag::None) {
    std::vector<Value> original_args;
    original_args.reserve(argc - 1);
    for (uint32_t index = 1; index < argc; ++index) original_args.push_back(args[index]);
    Value argument_tuple = Value::tuple(std::move(original_args));
    Value callback_args[3] = {out, args[0], argument_tuple};
    Value checked;
    if (!runtime_call_callable(runtime, errcheck, callback_args, 3, checked, error)) return false;
    if (!(checked.tag == ValueTag::Object && checked.as.obj == argument_tuple.as.obj))
      out = std::move(checked);
  }
  return true;
#endif
}

} // namespace xlang3
