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
#include "xlang3/builtin_methods.h"

#include "xlang3/functional_iterators.h"
#include "xlang3/mapping.h"
#include "xlang3/runtime.h"
#include "xlang3/sequence.h"
#include "xlang3/set_object.h"

namespace xlang3 {

namespace {

bool value_is_builtin_iterator(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr) {
    return false;
  }
  switch (value.as.obj->kind) {
    case ObjectKind::RangeIterator:
    case ObjectKind::SequenceIterator:
    case ObjectKind::DictIterator:
    case ObjectKind::SetIterator:
    case ObjectKind::EnumerateIterator:
    case ObjectKind::ZipIterator:
    case ObjectKind::MapIterator:
    case ObjectKind::FilterIterator:
    case ObjectKind::CallableIterator:
    case ObjectKind::ChainIterator:
    case ObjectKind::ProtocolIterator:
      return true;
    default:
      return false;
  }
}

bool iterator_iter_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 1, "iterator.__iter__", error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (!value_is_builtin_iterator(args[0])) {
    error = "iterator.__iter__ target is not an iterator";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

bool iterator_next_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 1, "iterator.__next__", error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value iterator = args[0];
  bool done = false;
  if (!sequence_iter_next(iterator, done, out, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (done) {
    error.clear();
    runtime.raise_class_error("StopIteration", error);
    return false;
  }
  return true;
}

static constexpr BuiltinMethodSpec kIteratorMethods[] = {
    {"__iter__", "iterator.__iter__", iterator_iter_method},
    {"__next__", "iterator.__next__", iterator_next_method},
};

} // namespace

bool iterator_get_method(const Value& object, const std::string& name, Value& out) {
  if (!value_is_builtin_iterator(object)) {
    return false;
  }
  return bind_builtin_method_from_table(object, name, kIteratorMethods, std::size(kIteratorMethods), out);
}

} // namespace xlang3
