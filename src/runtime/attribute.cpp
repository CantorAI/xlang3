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
#include "xlang3/attribute.h"

#include "xlang3/builtin_methods.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/runtime.h"

#include <cstdlib>
#include <iostream>

namespace xlang3 {

namespace {

bool missing_lookup_diagnostics_enabled() {
  static const bool enabled = std::getenv("XLANG3_DIAG_MISSING_LOOKUPS") != nullptr;
  return enabled;
}

const char* diagnostic_value_kind(const Value& value) {
  if (value.tag == ValueTag::Object && value.as.obj != nullptr) {
    switch (value.as.obj->kind) {
      case ObjectKind::String: return "str";
      case ObjectKind::Bytes: return "bytes";
      case ObjectKind::ByteArray: return "bytearray";
      case ObjectKind::MemoryView: return "memoryview";
      case ObjectKind::Slice: return "slice";
      case ObjectKind::Tuple: return "tuple";
      case ObjectKind::List: return "list";
      case ObjectKind::Dict: return "dict";
      case ObjectKind::Set: return "set";
      case ObjectKind::DictKeysView: return "dict_keys";
      case ObjectKind::DictValuesView: return "dict_values";
      case ObjectKind::DictItemsView: return "dict_items";
      case ObjectKind::DictIterator: return "dict_iterator";
      case ObjectKind::SetIterator: return "set_iterator";
      case ObjectKind::Range: return "range";
      case ObjectKind::RangeIterator: return "range_iterator";
      case ObjectKind::SequenceIterator: return "sequence_iterator";
      case ObjectKind::EnumerateIterator: return "enumerate";
      case ObjectKind::ZipIterator: return "zip";
      case ObjectKind::MapIterator: return "map";
      case ObjectKind::FilterIterator: return "filter";
      case ObjectKind::CallableIterator: return "callable_iterator";
      case ObjectKind::ChainIterator: return "chain_iterator";
      case ObjectKind::ProtocolIterator: return "protocol_iterator";
      case ObjectKind::Generator: return "generator";
      case ObjectKind::AsyncGeneratorAwaitable: return "async_generator_awaitable";
      case ObjectKind::Module: return "module";
      case ObjectKind::Cell: return "cell";
      case ObjectKind::Function: return "function";
      case ObjectKind::NativeFunction: return "native_function";
      case ObjectKind::Class: return "class";
      case ObjectKind::Instance: return "instance";
      case ObjectKind::BoundMethod: return "bound_method";
      case ObjectKind::StaticMethod: return "staticmethod";
      case ObjectKind::ClassMethod: return "classmethod";
      case ObjectKind::Super: return "super";
      case ObjectKind::SlotDescriptor: return "member_descriptor";
      case ObjectKind::Property: return "property";
      case ObjectKind::Code: return "code";
      case ObjectKind::Frame: return "frame";
      case ObjectKind::Traceback: return "traceback";
      case ObjectKind::File: return "file";
      case ObjectKind::GenericAlias: return "generic_alias";
      case ObjectKind::TypeParam: return "type_parameter";
    }
  }
  switch (value.tag) {
    case ValueTag::Invalid: return "invalid";
    case ValueTag::None: return "NoneType";
    case ValueTag::Bool: return "bool";
    case ValueTag::Int64: return "int";
    case ValueTag::Double: return "float";
    case ValueTag::Object: return "object";
    default: return "object";
  }
}

void emit_missing_attr_diagnostic(const Value& object, const std::string& name) {
  if (!missing_lookup_diagnostics_enabled()) {
    return;
  }
  std::cerr << "XLANG3_MISSING_ATTR kind=\"" << diagnostic_value_kind(object)
            << "\" attr=\"" << name << "\"\n";
}

bool none_new_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc == 0) {
    error = "NoneType.__new__(): not enough arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* klass = value_as_class(args[0]);
  if (klass == nullptr || klass->name != "NoneType") {
    const std::string receiver = klass == nullptr ? value_to_string(args[0]) : klass->name;
    error = "NoneType.__new__(" + receiver + "): " + receiver + " is not a subtype of NoneType";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  value_set_none(out);
  return true;
}

bool get_builtin_method(const Value& object, const std::string& name, Value& out) {
  if (object.tag == ValueTag::None && name == "__new__") {
    static Value none_new = Value::native_function(0, "NoneType.__new__", none_new_method);
    value_assign_fast(out, none_new);
    return true;
  }
  return list_get_method(object, name, out) ||
         tuple_get_method(object, name, out) ||
         dict_get_method(object, name, out) ||
         file_get_method(object, name, out) ||
         int_get_method(object, name, out) ||
         set_get_method(object, name, out) ||
         string_get_method(object, name, out) ||
         bytes_get_method(object, name, out) ||
         bytearray_get_method(object, name, out) ||
         memoryview_get_method(object, name, out) ||
         iterator_get_method(object, name, out) ||
         generator_get_method(object, name, out) ||
         property_get_method(object, name, out);
}

} // namespace

bool attribute_get(const Value& object, const std::string& name, Value& out, std::string& error) {
  if (value_as_module(object) != nullptr) {
    if (module_get_attr(object, name, out, error)) {
      return true;
    }
    if (get_builtin_method(object, name, out)) {
      error.clear();
      return true;
    }
    return false;
  }
  if (auto* function = value_as_function(object)) {
    if (name == "__annotations__") {
      if (function->annotations.tag == ValueTag::Invalid) {
        value_assign_fast(function->annotations, Value::dict({}));
      }
      value_assign_fast(out, function->annotations);
      return true;
    }
    const bool ok = object_get_attr(object, name, out, error);
    if (!ok) {
      emit_missing_attr_diagnostic(object, name);
    }
    return ok;
  }
  if (value_as_native_function(object) != nullptr || value_as_bound_method(object) != nullptr ||
      value_as_code(object) != nullptr || value_as_frame(object) != nullptr ||
      value_as_traceback(object) != nullptr || value_as_class(object) != nullptr ||
      value_as_instance(object) != nullptr || value_as_super(object) != nullptr ||
      value_as_static_method(object) != nullptr || value_as_class_method(object) != nullptr ||
      value_as_slot_descriptor(object) != nullptr || value_as_type_param(object) != nullptr ||
      value_as_generic_alias(object) != nullptr) {
    const bool ok = object_get_attr(object, name, out, error);
    if (!ok) {
      emit_missing_attr_diagnostic(object, name);
    }
    return ok;
  }
  if (get_builtin_method(object, name, out)) {
    return true;
  }
  if (value_as_memoryview(object) != nullptr) {
    const bool ok = object_get_attr(object, name, out, error);
    if (!ok) {
      emit_missing_attr_diagnostic(object, name);
    }
    return ok;
  }
  error = "object has no attribute '" + name + "'";
  emit_missing_attr_diagnostic(object, name);
  return false;
}

bool attribute_set(Value& object, const std::string& name, const Value& value, std::string& error) {
  if (value_as_module(object) != nullptr) {
    return module_set_attr(object, name, value, error);
  }
  return object_set_attr(object, name, value, error);
}

} // namespace xlang3
