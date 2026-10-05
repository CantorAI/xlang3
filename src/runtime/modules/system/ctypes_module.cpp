/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0 (the "License");
*/
#include "xlang3/builtins.h"
#include "ctypes_ffi.h"

#include "xlang3/functional_iterators.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"

#include <cerrno>
#include <ctime>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace xlang3 {
namespace {

thread_local int64_t ctypes_errno = 0;
thread_local int64_t ctypes_last_error = 0;
Value ctypes_array_base;

bool attr(const Value& object, const char* name, Value& out) {
  std::string ignored;
  return object_get_attr(object, name, out, ignored);
}

int64_t simple_type_size(const Value& type) {
  Value code_value;
  if (!attr(type, "_type_", code_value)) return 0;
  auto* code_string = value_as_string(code_value);
  if (code_string == nullptr) return 0;
  const auto code = string_object_view(*code_string);
  if (code.empty()) return 0;
  switch (code[0]) {
    case 'b': case 'B': case 'c': case '?': return 1;
    case 'h': case 'H': return 2;
    case 'i': case 'I': case 'l': case 'L': case 'f': return 4;
    case 'q': case 'Q': case 'd': case 'O': case 'P': case 'z': case 'Z': return 8;
    case 'u': return 2;
    case 'g': return 8;
    case 'F': return 8;
    case 'D': case 'G': return 16;
    case 'v': return 2;
    default: return 0;
  }
}

bool ctypes_sizeof(Runtime& runtime, const Value* args, uint32_t argc, Value& out,
                   std::string& error, void*) {
  if (argc != 1) {
    error = "sizeof() takes exactly one argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value type = args[0];
  if (auto* instance = value_as_instance(args[0])) type = instance->klass;
  size_t size = 0;
  size_t alignment = 0;
  if (!ctypes_type_layout(type, size, alignment)) {
    error = "this type has no size";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  out = Value::int64(static_cast<int64_t>(size));
  return true;
}

bool ctypes_alignment(Runtime& runtime, const Value* args, uint32_t argc, Value& out,
                      std::string& error, void*) {
  if (argc != 1) {
    error = "alignment() takes exactly one argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value type = args[0];
  if (auto* instance = value_as_instance(args[0])) type = instance->klass;
  size_t size = 0;
  size_t alignment = 0;
  if (!ctypes_type_layout(type, size, alignment)) {
    error = "this type has no alignment";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  out = Value::int64(static_cast<int64_t>(alignment));
  return true;
}

bool ctypes_simple_init(Runtime&, const Value* args, uint32_t argc, Value& out,
                        std::string& error, void*) {
  if (argc < 1 || argc > 2) { error = "_SimpleCData() takes at most one argument"; return false; }
  Value value = argc == 2 ? args[1] : Value::int64(0);
  Value self = args[0];
  if (!object_set_attr(self, "value", value, error)) return false;
  out = Value::none();
  return true;
}

bool ctypes_pointer_contents(Runtime& runtime, const Value* args, uint32_t argc,
                             Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "pointer contents require an instance";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return ctypes_pointer_read(runtime, args[0], 0, out, error);
}

bool ctypes_pointer_getitem(Runtime& runtime, const Value* args, uint32_t argc,
                            Value& out, std::string& error, void*) {
  if (argc != 2 || args[1].tag != ValueTag::Int64) {
    error = "pointer indices must be integers";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value element;
  if (!ctypes_pointer_read(runtime, args[0], args[1].as.i64, element, error)) return false;
  Value value;
  if (attr(element, "value", value)) {
    out = std::move(value);
  } else {
    out = std::move(element);
  }
  return true;
}

bool ctypes_pointer_bool(Runtime& runtime, const Value* args, uint32_t argc,
                         Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "pointer truth requires an instance";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value address;
  if (attr(args[0], "_address_", address) && address.tag == ValueTag::Int64) {
    out = Value::boolean(address.as.i64 != 0);
    return true;
  }
  Value referent;
  out = Value::boolean(attr(args[0], "value", referent) &&
                       referent.tag != ValueTag::None &&
                       !(referent.tag == ValueTag::Int64 && referent.as.i64 == 0));
  return true;
}

bool ctypes_structure_init(Runtime& runtime, const Value* args, uint32_t argc, Value& out,
                           std::string& error, void*) {
  if (argc < 1) {
    error = "Structure.__init__() requires an instance";
    return false;
  }
  auto* instance = value_as_instance(args[0]);
  if (instance == nullptr) {
    error = "Structure.__init__() requires a Structure instance";
    return false;
  }
  Value self = args[0];
  Value fields;
  if (!attr(instance->klass, "_fields_", fields)) {
    out = Value::none();
    return true;
  }
  auto* field_list = value_as_list(fields);
  auto* field_tuple = value_as_tuple(fields);
  if (field_list == nullptr && field_tuple == nullptr) {
    error = "_fields_ must be a sequence of field definitions";
    return false;
  }
  const auto& definitions = field_list != nullptr ? field_list->items : field_tuple->items;
  if (argc - 1 > definitions.size()) {
    error = "too many initializers";
    return false;
  }
  for (size_t index = 0; index < definitions.size(); ++index) {
    auto* definition = value_as_tuple(definitions[index]);
    if (definition == nullptr || definition->items.size() < 2) continue;
    auto* name = value_as_string(definition->items[0]);
    if (name == nullptr) continue;
    Value field_value;
    if (index + 1 < argc) {
      field_value = args[index + 1];
    } else if (simple_type_size(definition->items[1]) != 0) {
      field_value = Value::int64(0);
    } else {
      if (!runtime_call_callable(runtime, definition->items[1], nullptr, 0, field_value, error)) {
        return false;
      }
    }
    if (!object_set_attr(self, string_object_to_string(*name), field_value, error)) {
      return false;
    }
  }
  out = Value::none();
  return true;
}

bool ctypes_identity(Runtime&, const Value* args, uint32_t argc, Value& out,
                     std::string& error, void*) {
  if (argc < 1) { error = "expected an argument"; return false; }
  out = args[argc - 1];
  return true;
}

bool ctypes_array_mul(Runtime& runtime, const Value* args, uint32_t argc, Value& out,
                      std::string& error, void*) {
  if (argc != 2 || value_as_class(args[0]) == nullptr || args[1].tag != ValueTag::Int64 || args[1].as.i64 < 0) {
    error = "can't multiply ctypes type by non-negative integer";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* element_class = value_as_class(args[0]);
  std::vector<std::pair<std::string, Value>> attrs{
      {"__module__", Value::string("ctypes")},
      {"_type_", args[0]},
      {"_length_", args[1]},
  };
  std::string name = element_class->name + "_Array_" + std::to_string(args[1].as.i64);
  out = Value::class_object(name, std::move(attrs), ctypes_array_base,
                            {}, element_class->metaclass);
  return true;
}

bool ctypes_array_init(Runtime& runtime, const Value* args, uint32_t argc, Value& out,
                       std::string& error, void*) {
  if (argc < 1 || value_as_instance(args[0]) == nullptr) {
    error = "Array.__init__() requires an array instance";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value length_value;
  Value element_type;
  const Value& array_type = value_as_instance(args[0])->klass;
  if (!attr(array_type, "_length_", length_value) || length_value.tag != ValueTag::Int64 ||
      length_value.as.i64 < 0 || !attr(array_type, "_type_", element_type)) {
    error = "invalid ctypes array type";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const size_t length = static_cast<size_t>(length_value.as.i64);
  if (argc - 1 > length) {
    error = "too many initializers";
    runtime.raise_class_error("IndexError", error);
    return false;
  }
  std::vector<Value> items;
  items.reserve(length);
  for (size_t index = 0; index < length; ++index) {
    Value item;
    const Value* initializer = index + 1 < argc ? &args[index + 1] : nullptr;
    if (initializer != nullptr && value_as_instance(*initializer) != nullptr &&
        value_as_instance(*initializer)->klass.tag == ValueTag::Object &&
        element_type.tag == ValueTag::Object &&
        value_as_instance(*initializer)->klass.as.obj == element_type.as.obj) {
      item = *initializer;
    } else if (!runtime_call_callable(runtime, element_type, initializer,
                                      initializer == nullptr ? 0 : 1, item, error)) {
      return false;
    }
    items.push_back(std::move(item));
  }
  Value self = args[0];
  if (!object_set_attr(self, "_array_items_", Value::list(std::move(items)), error)) return false;
  out = Value::none();
  return true;
}

bool ctypes_array_len(Runtime& runtime, const Value* args, uint32_t argc, Value& out,
                      std::string& error, void*) {
  if (argc != 1 || value_as_instance(args[0]) == nullptr) {
    error = "expected ctypes array";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value items;
  if (!attr(args[0], "_array_items_", items) || value_as_list(items) == nullptr) {
    error = "ctypes array is uninitialized";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  out = Value::int64(static_cast<int64_t>(value_as_list(items)->items.size()));
  return true;
}

bool ctypes_array_getitem(Runtime& runtime, const Value* args, uint32_t argc, Value& out,
                          std::string& error, void*) {
  if (argc != 2 || args[1].tag != ValueTag::Int64) {
    error = "array indices must be integers";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value items;
  auto* list = attr(args[0], "_array_items_", items) ? value_as_list(items) : nullptr;
  if (list == nullptr) {
    error = "ctypes array is uninitialized";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  int64_t index = args[1].as.i64;
  if (index < 0) index += static_cast<int64_t>(list->items.size());
  if (index < 0 || static_cast<size_t>(index) >= list->items.size()) {
    error = "invalid index";
    runtime.raise_class_error("IndexError", error);
    return false;
  }
  Value element = list->items[static_cast<size_t>(index)];
  Value element_type;
  Value code;
  if (value_as_instance(args[0]) != nullptr &&
      attr(value_as_instance(args[0])->klass, "_type_", element_type) &&
      attr(element_type, "_type_", code) && value_as_string(code) != nullptr &&
      attr(element, "value", out)) return true;
  out = std::move(element);
  return true;
}

bool ctypes_array_setitem(Runtime& runtime, const Value* args, uint32_t argc, Value& out,
                          std::string& error, void*) {
  if (argc != 3 || args[1].tag != ValueTag::Int64) {
    error = "array indices must be integers";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value items;
  auto* list = attr(args[0], "_array_items_", items) ? value_as_list(items) : nullptr;
  if (list == nullptr || value_as_instance(args[0]) == nullptr) {
    error = "ctypes array is uninitialized";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  int64_t index = args[1].as.i64;
  if (index < 0) index += static_cast<int64_t>(list->items.size());
  if (index < 0 || static_cast<size_t>(index) >= list->items.size()) {
    error = "invalid index";
    runtime.raise_class_error("IndexError", error);
    return false;
  }
  Value element_type;
  if (!attr(value_as_instance(args[0])->klass, "_type_", element_type)) {
    error = "invalid ctypes array type";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value element;
  if (value_as_instance(args[2]) != nullptr &&
      value_as_instance(args[2])->klass.tag == ValueTag::Object &&
      element_type.tag == ValueTag::Object &&
      value_as_instance(args[2])->klass.as.obj == element_type.as.obj) {
    element = args[2];
  } else if (!runtime_call_callable(runtime, element_type, args + 2, 1, element, error)) {
    return false;
  }
  list->items[static_cast<size_t>(index)] = std::move(element);
  out = Value::none();
  return true;
}

bool ctypes_function_init(Runtime&, const Value* args, uint32_t argc, Value& out,
                          std::string& error, void*) {
  if (argc < 1 || argc > 2) { error = "CFuncPtr() takes one argument"; return false; }
  Value self = args[0];
  if (argc == 2 && !object_set_attr(self, "_target_", args[1], error)) return false;
  out = Value::none();
  return true;
}

bool ctypes_function_call(Runtime& runtime, const Value* args, uint32_t argc,
                          Value& out, std::string& error, void*) {
  return ctypes_foreign_call(runtime, args, argc, out, error);
}

bool ctypes_load_library(Runtime& runtime, const Value* args, uint32_t argc,
                         Value& out, std::string& error, void*) {
  return ctypes_foreign_load_library(runtime, args, argc, out, error);
}

bool ctypes_get_errno(Runtime&, const Value*, uint32_t argc, Value& out,
                      std::string& error, void*) {
  if (argc != 0) { error = "get_errno() takes no arguments"; return false; }
  out = Value::int64(ctypes_errno == 0 ? errno : ctypes_errno);
  return true;
}

bool ctypes_set_errno(Runtime&, const Value* args, uint32_t argc, Value& out,
                      std::string& error, void*) {
  if (argc != 1 || args[0].tag != ValueTag::Int64) { error = "set_errno() expects an integer"; return false; }
  out = Value::int64(ctypes_errno);
  ctypes_errno = args[0].as.i64;
  errno = static_cast<int>(ctypes_errno);
  return true;
}

bool ctypes_get_last_error(Runtime&, const Value*, uint32_t argc, Value& out,
                           std::string& error, void*) {
  if (argc != 0) { error = "get_last_error() takes no arguments"; return false; }
#if defined(_WIN32)
  out = Value::int64(ctypes_last_error == 0 ? GetLastError() : ctypes_last_error);
#else
  out = Value::int64(ctypes_last_error);
#endif
  return true;
}

bool ctypes_set_last_error(Runtime&, const Value* args, uint32_t argc, Value& out,
                           std::string& error, void*) {
  if (argc != 1 || args[0].tag != ValueTag::Int64) { error = "set_last_error() expects an integer"; return false; }
  out = Value::int64(ctypes_last_error);
  ctypes_last_error = args[0].as.i64;
#if defined(_WIN32)
  SetLastError(static_cast<DWORD>(ctypes_last_error));
#endif
  return true;
}

bool ctypes_format_error(Runtime&, const Value* args, uint32_t argc, Value& out,
                         std::string& error, void*) {
  if (argc > 1) { error = "FormatError() takes at most one argument"; return false; }
#if defined(_WIN32)
  const DWORD code = argc == 1 && args[0].tag == ValueTag::Int64
      ? static_cast<DWORD>(args[0].as.i64) : GetLastError();
  char buffer[512]{};
  const DWORD length = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                      nullptr, code, 0, buffer, sizeof(buffer), nullptr);
  out = Value::string(length == 0 ? std::string("Windows error ") + std::to_string(code)
                                  : std::string(buffer, length));
#else
  out = Value::string("system error");
#endif
  return true;
}

bool ctypes_address(Runtime&, const Value* args, uint32_t argc, Value& out,
                    std::string& error, void*) {
  if (argc != 1) { error = "expected one ctypes instance"; return false; }
  out = Value::int64(reinterpret_cast<int64_t>(args[0].tag == ValueTag::Object ? args[0].as.obj : nullptr));
  return true;
}

bool ctypes_resize(Runtime&, const Value*, uint32_t argc, Value& out,
                   std::string& error, void*) {
  if (argc != 2) { error = "resize() takes two arguments"; return false; }
  out = Value::none();
  return true;
}

Value make_base(Runtime& runtime, const char* name, const Value& metaclass,
                bool scalar = false, bool function = false) {
  std::vector<std::pair<std::string, Value>> attrs{{"__module__", Value::string("_ctypes")}};
  if (scalar) {
    attrs.push_back({"__init__", runtime.make_native_function(std::string("_ctypes.") + name + ".__init__", ctypes_simple_init)});
    attrs.push_back({"from_param", Value::class_method(runtime.make_native_function(std::string("_ctypes.") + name + ".from_param", ctypes_identity))});
    attrs.push_back({"from_buffer", Value::class_method(runtime.make_native_function(std::string("_ctypes.") + name + ".from_buffer", ctypes_identity))});
  }
  if (function) {
    attrs.push_back({"__init__", runtime.make_native_function("_ctypes.CFuncPtr.__init__", ctypes_function_init)});
    attrs.push_back({"__call__", runtime.make_native_function("_ctypes.CFuncPtr.__call__", ctypes_function_call)});
  }
  return Value::class_object(name, std::move(attrs), Value::invalid(), {}, metaclass);
}

} // namespace

void register_ctypes_module(Runtime& runtime) {
  Value type_type = runtime.find_builtin("type") ? *runtime.find_builtin("type") : Value::invalid();
  std::vector<std::pair<std::string, Value>> meta_attrs{
      {"__module__", Value::string("_ctypes")},
      {"__mul__", runtime.make_native_function("_ctypes.PyCSimpleType.__mul__", ctypes_array_mul)},
      {"__rmul__", runtime.make_native_function("_ctypes.PyCSimpleType.__rmul__", ctypes_array_mul)},
  };
  Value ctypes_meta = Value::class_object("PyCSimpleType", std::move(meta_attrs), type_type, {}, type_type);
  Value simple = make_base(runtime, "_SimpleCData", ctypes_meta, true);
  ctypes_array_base = make_base(runtime, "Array", ctypes_meta);
  value_as_class(ctypes_array_base)->attrs["__init__"] =
      runtime.make_native_function("_ctypes.Array.__init__", ctypes_array_init);
  value_as_class(ctypes_array_base)->attrs["__len__"] =
      runtime.make_native_function("_ctypes.Array.__len__", ctypes_array_len);
  value_as_class(ctypes_array_base)->attrs["__getitem__"] =
      runtime.make_native_function("_ctypes.Array.__getitem__", ctypes_array_getitem);
  value_as_class(ctypes_array_base)->attrs["__setitem__"] =
      runtime.make_native_function("_ctypes.Array.__setitem__", ctypes_array_setitem);
  Value structure = make_base(runtime, "Structure", ctypes_meta);
  Value union_type = make_base(runtime, "Union", ctypes_meta);
  value_as_class(structure)->attrs["__init__"] =
      runtime.make_native_function("_ctypes.Structure.__init__", ctypes_structure_init);
  value_as_class(union_type)->attrs["__init__"] =
      runtime.make_native_function("_ctypes.Union.__init__", ctypes_structure_init);
  Value pointer_type = make_base(runtime, "_Pointer", ctypes_meta, true);
  value_as_class(pointer_type)->attrs["contents"] = Value::property(
      runtime.make_native_function("_ctypes._Pointer.contents", ctypes_pointer_contents),
      Value::none(), Value::none(), Value::none());
  value_as_class(pointer_type)->attrs["__getitem__"] =
      runtime.make_native_function("_ctypes._Pointer.__getitem__", ctypes_pointer_getitem);
  value_as_class(pointer_type)->attrs["__bool__"] =
      runtime.make_native_function("_ctypes._Pointer.__bool__", ctypes_pointer_bool);
  Value cfunc = make_base(runtime, "CFuncPtr", ctypes_meta, false, true);
  Value cfield = make_base(runtime, "CField", ctypes_meta);

  Value exception = runtime.find_builtin("Exception") ? *runtime.find_builtin("Exception") : Value::invalid();
  Value argument_error = Value::class_object("ArgumentError", {{"__module__", Value::string("_ctypes")}}, exception);
  Value com_error = Value::class_object("COMError", {{"__module__", Value::string("_ctypes")}}, exception);

  NativeModuleBuilder builder(runtime, "_ctypes");
  builder.value("__version__", Value::string("1.1.0"))
      .value("Union", union_type).value("Structure", structure).value("Array", ctypes_array_base)
      .value("_Pointer", pointer_type).value("CFuncPtr", cfunc).value("_SimpleCData", simple)
      .value("CField", cfield).value("ArgumentError", argument_error).value("COMError", com_error)
      .value("RTLD_LOCAL", Value::int64(0)).value("RTLD_GLOBAL", Value::int64(0x100))
      .value("SIZEOF_TIME_T", Value::int64(sizeof(std::time_t)))
      .value("FUNCFLAG_CDECL", Value::int64(1)).value("FUNCFLAG_STDCALL", Value::int64(0))
      .value("FUNCFLAG_PYTHONAPI", Value::int64(4)).value("FUNCFLAG_USE_ERRNO", Value::int64(8))
      .value("FUNCFLAG_USE_LASTERROR", Value::int64(16))
      .value("_memmove_addr", Value::int64(1)).value("_memset_addr", Value::int64(2))
      .value("_string_at_addr", Value::int64(3)).value("_wstring_at_addr", Value::int64(4))
      .value("_cast_addr", Value::int64(5)).value("_memoryview_at_addr", Value::int64(6))
      .function("sizeof", ctypes_sizeof).function("alignment", ctypes_alignment)
      .function("byref", ctypes_identity).function("addressof", ctypes_address)
      .function("resize", ctypes_resize).function("get_errno", ctypes_get_errno)
      .function("set_errno", ctypes_set_errno).function("get_last_error", ctypes_get_last_error)
      .function("set_last_error", ctypes_set_last_error).function("LoadLibrary", ctypes_load_library)
      .function("FormatError", ctypes_format_error).function("CopyComPointer", ctypes_identity)
      .function("_check_HRESULT", ctypes_identity).function("buffer_info", ctypes_identity);
#if !defined(_WIN32)
  builder.value("RTLD_LOCAL", Value::int64(RTLD_LOCAL))
      .value("RTLD_GLOBAL", Value::int64(RTLD_GLOBAL))
      .function("dlopen", ctypes_load_library);
#endif
  runtime.register_module("_ctypes", builder.finish());
}

} // namespace xlang3
