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

#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"

#include <string>
#include <vector>

namespace xlang3 {

namespace {

bool get_string_value(const Value& value, std::string& out) {
  if (auto* str = value_as_string(value)) {
    out = string_object_to_string(*str);
    return true;
  }
  return false;
}

bool class_has_fields(const Value& klass) {
  Value ignored;
  std::string error;
  return object_get_attr(klass, "_fields_", ignored, error);
}

Value make_primitive_class(Runtime& runtime, const std::string& name);
bool ctypes_structure_init(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*);

Value make_default_field_value(Runtime& runtime, const Value& field_type, std::string& error) {
  if (auto* klass = value_as_class(field_type)) {
    if (class_has_fields(field_type)) {
      Value nested = Value::instance(field_type);
      Value ignored;
      Value init_args[] = {nested};
      if (!ctypes_structure_init(runtime, init_args, 1, ignored, error, nullptr)) return Value::invalid();
      return nested;
    }
    (void)klass;
  }
  (void)runtime;
  return Value::int64(0);
}

bool ctypes_scalar_init(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "ctypes scalar constructor expected optional value";
    return false;
  }
  Value self = args[0];
  Value value = argc == 2 ? args[1] : Value::int64(0);
  if (value.tag == ValueTag::Bool) {
    value = Value::int64(value.as.b ? 1 : 0);
  }
  if (!object_set_attr(self, "value", value, error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool ctypes_scalar_int(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "ctypes scalar __int__ expected no arguments";
    return false;
  }
  Value value;
  if (!object_get_attr(args[0], "value", value, error)) {
    return false;
  }
  if (value.tag == ValueTag::Int64) {
    value_assign_fast(out, value);
    return true;
  }
  if (value.tag == ValueTag::Bool) {
    out = Value::int64(value.as.b ? 1 : 0);
    return true;
  }
  out = Value::int64(0);
  return true;
}

bool ctypes_structure_init(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "ctypes.Structure.__init__ expected no arguments";
    return false;
  }

  Value fields;
  if (!object_get_attr(args[0], "_fields_", fields, error)) {
    value_set_none(out);
    return true;
  }

  auto assign_field = [&](const Value& item) -> bool {
    auto* pair = value_as_tuple(item);
    if (pair == nullptr || pair->items.size() < 2) {
      error = "ctypes _fields_ entries must be (name, type)";
      return false;
    }
    std::string field_name;
    if (!get_string_value(pair->items[0], field_name)) {
      error = "ctypes field name must be str";
      return false;
    }
    Value field_value = make_default_field_value(runtime, pair->items[1], error);
    if (field_value.tag == ValueTag::Invalid) {
      return false;
    }
    Value self = args[0];
    return object_set_attr(self, field_name, field_value, error);
  };

  if (auto* tuple = value_as_tuple(fields)) {
    for (const auto& item : tuple->items) {
      if (!assign_field(item)) return false;
    }
  } else if (auto* list = value_as_list(fields)) {
    for (const auto& item : list->items) {
      if (!assign_field(item)) return false;
    }
  }

  value_set_none(out);
  return true;
}

bool ctypes_create_unicode_buffer(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "ctypes.create_unicode_buffer() expected init or size";
    return false;
  }
  out = Value::string("");
  return true;
}

bool ctypes_pointer_type(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "ctypes.POINTER() expected type";
    return false;
  }
  out = Value::class_object("LP_" + object_model_to_string(args[0]), {{"_type_", args[0]}});
  return true;
}

void value_user_data_cleanup(void* data) {
  delete static_cast<Value*>(data);
}

Value make_pointer_value(const Value& pointer_class, const Value& target, std::string& error) {
  Value pointer = Value::instance(pointer_class);
  if (!object_set_attr(pointer, "contents", target, error)) {
    return Value::invalid();
  }
  return pointer;
}

bool ctypes_pointer(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 1) {
    error = "ctypes.pointer() expected object";
    return false;
  }
  auto* pointer_class = static_cast<Value*>(user_data);
  if (pointer_class == nullptr) {
    error = "ctypes pointer class unavailable";
    return false;
  }
  out = make_pointer_value(*pointer_class, args[0], error);
  return out.tag != ValueTag::Invalid;
}

bool ctypes_byref(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc < 1 || argc > 2) {
    error = "ctypes.byref() expected object";
    return false;
  }
  auto* pointer_class = static_cast<Value*>(user_data);
  if (pointer_class == nullptr) {
    error = "ctypes pointer class unavailable";
    return false;
  }
  out = make_pointer_value(*pointer_class, args[0], error);
  return out.tag != ValueTag::Invalid;
}

bool ctypes_cast(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 2) {
    error = "ctypes.cast() expected object and type";
    return false;
  }
  Value contents;
  std::string attr_error;
  if (object_get_attr(args[0], "contents", contents, attr_error)) {
    value_assign_fast(out, args[0]);
    return true;
  }
  auto* pointer_class = static_cast<Value*>(user_data);
  if (pointer_class == nullptr) {
    error = "ctypes pointer class unavailable";
    return false;
  }
  out = make_pointer_value(*pointer_class, args[0], error);
  return out.tag != ValueTag::Invalid;
}

bool ctypes_addressof(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "ctypes.addressof() expected object";
    return false;
  }
  uintptr_t address = 0;
  if (args[0].tag == ValueTag::Object) {
    address = reinterpret_cast<uintptr_t>(args[0].as.obj);
  } else {
    address = reinterpret_cast<uintptr_t>(&args[0]);
  }
  out = Value::int64(static_cast<int64_t>(address == 0 ? 1 : address));
  return true;
}

bool ctypes_memmove(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 3) {
    error = "ctypes.memmove() expected dst, src, count";
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

bool ctypes_memset(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 3) {
    error = "ctypes.memset() expected dst, value, count";
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

bool ctypes_create_string_buffer(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "ctypes.create_string_buffer() expected init or size";
    return false;
  }
  if (auto* str = value_as_string(args[0])) {
    out = Value::bytearray(string_object_to_string(*str));
    return true;
  }
  if (auto* bytes = value_as_bytes(args[0])) {
    out = Value::bytearray(bytes_object_to_string(*bytes));
    return true;
  }
  if (args[0].tag == ValueTag::Int64) {
    out = Value::bytearray(std::string(static_cast<size_t>(args[0].as.i64), '\0'));
    return true;
  }
  out = Value::bytearray("");
  return true;
}

bool ctypes_sizeof(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "ctypes.sizeof() expected object";
    return false;
  }
  out = Value::int64(64);
  return true;
}

bool ctypes_win_error(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc > 1) {
    error = "ctypes.WinError() expected optional code";
    return false;
  }
  out = runtime.make_exception("OSError", "Windows API call failed");
  return true;
}

int64_t kernel_result_for(const std::string& name) {
  if (name == "CreateJobObjectA" || name == "OpenProcess") {
    return 1;
  }
  return 1;
}

bool ctypes_cfunc_call(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1) {
    error = "ctypes function expected self";
    return false;
  }
  Value name_value;
  std::string name;
  if (object_get_attr(args[0], "__name__", name_value, error)) {
    get_string_value(name_value, name);
  } else {
    error.clear();
  }
  out = Value::int64(kernel_result_for(name));
  return true;
}

Value make_primitive_class(Runtime& runtime, const std::string& name) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__init__", runtime.make_native_function("ctypes." + name + ".__init__", ctypes_scalar_init)});
  attrs.push_back({"__int__", runtime.make_native_function("ctypes." + name + ".__int__", ctypes_scalar_int)});
  return Value::class_object(name, std::move(attrs));
}

Value make_cfunc_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__call__", runtime.make_native_function("ctypes._CFuncPtr.__call__", ctypes_cfunc_call)});
  return Value::class_object("_CFuncPtr", std::move(attrs));
}

Value make_pointer_class() {
  return Value::class_object("_Pointer", {});
}

Value make_cfunc(Runtime& runtime, const Value& cfunc_class, const std::string& name) {
  (void)runtime;
  Value function = Value::instance(cfunc_class);
  std::string error;
  object_set_attr(function, "__name__", Value::string(name), error);
  return function;
}

Value make_kernel32(Runtime& runtime, const Value& cfunc_class) {
  Value library = Value::instance(Value::class_object("_WinDLL", {}));
  std::string error;
  for (const char* name : {
           "AssignProcessToJobObject",
           "CreateJobObjectA",
           "OpenProcess",
           "QueryInformationJobObject",
           "SetInformationJobObject",
           "TerminateJobObject",
       }) {
    object_set_attr(library, name, make_cfunc(runtime, cfunc_class, name), error);
  }
  return library;
}

} // namespace

void register_ctypes_module(Runtime& runtime) {
  Value structure_class = Value::class_object(
      "Structure",
      {{"__init__", runtime.make_native_function("ctypes.Structure.__init__", ctypes_structure_init)}});

  Value c_int = make_primitive_class(runtime, "c_int");
  Value c_void_p = make_primitive_class(runtime, "c_void_p");
  Value c_size_t = make_primitive_class(runtime, "c_size_t");
  Value c_ulonglong = make_primitive_class(runtime, "c_ulonglong");
  Value c_uint = make_primitive_class(runtime, "c_uint");
  Value c_longlong = make_primitive_class(runtime, "c_longlong");
  Value c_char_p = make_primitive_class(runtime, "c_char_p");
  Value c_bool = make_primitive_class(runtime, "c_bool");
  Value cfunc_class = make_cfunc_class(runtime);
  Value pointer_class = make_pointer_class();

  NativeModuleBuilder wintypes_builder(runtime, "ctypes.wintypes");
  wintypes_builder.value("MAX_PATH", Value::int64(260))
      .value("LPCWSTR", c_char_p)
      .value("LPWSTR", c_char_p)
      .value("BOOL", c_bool)
      .value("DWORD", c_uint)
      .value("HANDLE", c_void_p)
      .value("LARGE_INTEGER", c_longlong)
      .value("LPCSTR", c_char_p)
      .value("UINT", c_uint);
  Value wintypes = wintypes_builder.finish();
  runtime.register_module("ctypes.wintypes", wintypes);

  Value windll = Value::instance(Value::class_object("_LibraryLoader", {}));
  std::string error;
  object_set_attr(windll, "kernel32", make_kernel32(runtime, cfunc_class), error);

  NativeModuleBuilder builder(runtime, "ctypes");
  builder.function("create_unicode_buffer", ctypes_create_unicode_buffer)
      .function("POINTER", ctypes_pointer_type)
      .function("sizeof", ctypes_sizeof)
      .function("WinError", ctypes_win_error)
      .function("memmove", ctypes_memmove)
      .function("memset", ctypes_memset)
      .function("create_string_buffer", ctypes_create_string_buffer)
      .value("Structure", structure_class)
      .value("_Pointer", pointer_class)
      .value("c_int", c_int)
      .value("c_void_p", c_void_p)
      .value("c_size_t", c_size_t)
      .value("c_ulonglong", c_ulonglong)
      .value("c_uint", c_uint)
      .value("c_longlong", c_longlong)
      .value("c_char_p", c_char_p)
      .value("c_bool", c_bool)
      .value("windll", windll)
      .value("wintypes", wintypes);
  builder.value("pointer", runtime.make_native_function("ctypes.pointer", ctypes_pointer, new Value(pointer_class), value_user_data_cleanup));
  builder.value("byref", runtime.make_native_function("ctypes.byref", ctypes_byref, new Value(pointer_class), value_user_data_cleanup));
  builder.value("cast", runtime.make_native_function("ctypes.cast", ctypes_cast, new Value(pointer_class), value_user_data_cleanup));
  builder.value("addressof", runtime.make_native_function("ctypes.addressof", ctypes_addressof));
  runtime.register_module("ctypes", builder.finish());
}

} // namespace xlang3
