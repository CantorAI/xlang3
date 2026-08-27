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

#include "../xlang_frame.h"
#include "../xlang_vm_op_switch.h"

#include "xlang3/functional_iterators.h"
#include "xlang3/mapping.h"
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"
#include "xlang3/set_object.h"
#include "xlang3/value_hash.h"

#include <string>
#include <utility>
#include <vector>

namespace xlang3::xlang_vm::ops {

XLANG3_HOT_INLINE XlangVMOpFlow make_tuple(
    const ir::Instr& in,
    const ir::Function& fn,
    XlangVMSmallRegisterBuffer& regs,
    RuntimeResult& result) {
  if (in.a >= fn.tuple_items.size()) {
    result.errors.push_back("invalid tuple item list");
    return XlangVMOpFlow::ReturnResult;
  }
  regs[in.dst] = Value::tuple_reserved(fn.tuple_items[in.a].size());
  auto* tuple = value_as_tuple(regs[in.dst]);
  for (const auto reg : fn.tuple_items[in.a]) {
    if (reg >= regs.size()) {
      result.errors.push_back("invalid tuple item register");
      return XlangVMOpFlow::ReturnResult;
    }
    tuple->items.push_back_unchecked(regs[reg]);
  }
  return XlangVMOpFlow::Next;
}

XLANG3_HOT_INLINE XlangVMOpFlow make_list(
    const ir::Instr& in,
    const ir::Function& fn,
    XlangVMSmallRegisterBuffer& regs,
    RuntimeResult& result) {
  if (in.a >= fn.list_items.size()) {
    result.errors.push_back("invalid list item list");
    return XlangVMOpFlow::ReturnResult;
  }
  regs[in.dst] = Value::list_reserved(fn.list_items[in.a].size());
  auto* list = value_as_list(regs[in.dst]);
  for (const auto reg : fn.list_items[in.a]) {
    if (reg >= regs.size()) {
      result.errors.push_back("invalid list item register");
      return XlangVMOpFlow::ReturnResult;
    }
    list->items.push_back(regs[reg]);
  }
  return XlangVMOpFlow::Next;
}

template <typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow make_dict(
    const ir::Instr& in,
    const ir::Function& fn,
    XlangVMSmallRegisterBuffer& regs,
    RuntimeResult& result,
    Runtime& runtime,
    RaiseExceptionValue&& raise_exception_value) {
  if (in.a >= fn.dict_items.size()) {
    result.errors.push_back("invalid dict item list");
    return XlangVMOpFlow::ReturnResult;
  }
  regs[in.dst] = Value::dict_reserved(fn.dict_items[in.a].size());
  auto* dict = value_as_dict(regs[in.dst]);
  for (const auto& pair : fn.dict_items[in.a]) {
    if (pair.first >= regs.size() || pair.second >= regs.size()) {
      result.errors.push_back("invalid dict item register");
      return XlangVMOpFlow::ReturnResult;
    }
    size_t ignored_hash = 0;
    std::string error;
    if (!value_hash_key(regs[pair.first], ignored_hash, error)) {
      return raise_exception_value(runtime.make_exception("TypeError", error))
                 ? XlangVMOpFlow::ContinueLoop
                 : XlangVMOpFlow::ReturnResult;
    }
    bool replaced = false;
    for (auto& entry : dict->entries) {
      if (value_key_equal(entry.first, regs[pair.first])) {
        value_assign_fast(entry.second, regs[pair.second]);
        replaced = true;
        break;
      }
    }
    if (!replaced) {
      dict->entries.emplace_back(regs[pair.first], regs[pair.second]);
    }
  }
  return XlangVMOpFlow::Next;
}

template <typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow make_set(
    const ir::Instr& in,
    const ir::Function& fn,
    XlangVMSmallRegisterBuffer& regs,
    RuntimeResult& result,
    Runtime& runtime,
    RaiseExceptionValue&& raise_exception_value) {
  if (in.a >= fn.set_items.size()) {
    result.errors.push_back("invalid set item list");
    return XlangVMOpFlow::ReturnResult;
  }
  regs[in.dst] = Value::set({});
  for (const auto reg : fn.set_items[in.a]) {
    if (reg >= regs.size()) {
      result.errors.push_back("invalid set item register");
      return XlangVMOpFlow::ReturnResult;
    }
    std::string error;
    if (!::xlang3::set_add(regs[in.dst], regs[reg], error)) {
      if (error.find("not hashable") != std::string::npos) {
        return raise_exception_value(runtime.make_exception("TypeError", error))
                   ? XlangVMOpFlow::ContinueLoop
                   : XlangVMOpFlow::ReturnResult;
      }
      result.errors.push_back(error);
      return XlangVMOpFlow::ReturnResult;
    }
  }
  return XlangVMOpFlow::Next;
}

XLANG3_HOT_INLINE XlangVMOpFlow make_slice(
    const ir::Instr& in,
    XlangVMSmallRegisterBuffer& regs,
    RuntimeResult& result) {
  if (in.a >= regs.size() || in.b >= regs.size() || in.c >= regs.size()) {
    result.errors.push_back("invalid slice registers");
    return XlangVMOpFlow::ReturnResult;
  }
  regs[in.dst] = Value::slice(regs[in.a], regs[in.b], regs[in.c]);
  return XlangVMOpFlow::Next;
}

template <typename RaiseRuntimeError>
XLANG3_HOT_INLINE XlangVMOpFlow list_append(
    const ir::Instr& in,
    XlangVMSmallRegisterBuffer& regs,
    RaiseRuntimeError&& raise_runtime_error) {
  if (auto* list = value_as_list(regs[in.dst])) {
    if (list->items.empty()) {
      list->items.reserve(64);
    }
    list->items.push_back(regs[in.a]);
    return XlangVMOpFlow::Next;
  }
  std::string error;
  if (!sequence_list_append(regs[in.dst], regs[in.a], error)) {
    return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  return XlangVMOpFlow::Next;
}

template <typename RaiseRuntimeError>
XLANG3_HOT_INLINE XlangVMOpFlow list_extend(
    const ir::Instr& in,
    XlangVMSmallRegisterBuffer& regs,
    RaiseRuntimeError&& raise_runtime_error) {
  std::string error;
  Value iterator;
  if (!sequence_get_iter(regs[in.a], iterator, error)) {
    return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  for (;;) {
    bool done = false;
    Value item;
    if (!sequence_iter_next(iterator, done, item, error)) {
      return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
    if (done) {
      break;
    }
    if (!sequence_list_append(regs[in.dst], item, error)) {
      return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
  }
  return XlangVMOpFlow::Next;
}

template <typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow dict_set(
    const ir::Instr& in,
    XlangVMSmallRegisterBuffer& regs,
    Runtime& runtime,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  if (auto* dict = value_as_dict(regs[in.dst])) {
    const auto& key = regs[in.a];
    if (key.tag == ValueTag::Int64) {
      if (dict->entries.empty()) {
        dict->entries.reserve(64);
      }
      bool replaced = false;
      for (auto& entry : dict->entries) {
        if (entry.first.tag == ValueTag::Int64 && entry.first.as.i64 == key.as.i64) {
          value_assign_fast(entry.second, regs[in.b]);
          replaced = true;
          break;
        }
      }
      if (!replaced) {
        dict->entries.push_back(std::make_pair(key, regs[in.b]));
      }
      return XlangVMOpFlow::Next;
    }
  }
  std::string error;
  if (!sequence_set_item(regs[in.dst], regs[in.a], regs[in.b], error)) {
    if (error.find("not hashable") != std::string::npos) {
      return raise_exception_value(runtime.make_exception("TypeError", error))
                 ? XlangVMOpFlow::ContinueLoop
                 : XlangVMOpFlow::ReturnResult;
    }
    return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  return XlangVMOpFlow::Next;
}

template <typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow set_add_op(
    const ir::Instr& in,
    XlangVMSmallRegisterBuffer& regs,
    Runtime& runtime,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  if (auto* set = value_as_set(regs[in.dst])) {
    const auto& item = regs[in.a];
    if (item.tag == ValueTag::Int64) {
      if (set->items.empty()) {
        set->items.reserve(32);
      }
      bool exists = false;
      for (const auto& existing : set->items) {
        if (existing.tag == ValueTag::Int64 && existing.as.i64 == item.as.i64) {
          exists = true;
          break;
        }
      }
      if (!exists) {
        set->items.push_back(item);
      }
      return XlangVMOpFlow::Next;
    }
  }
  std::string error;
  if (!::xlang3::set_add(regs[in.dst], regs[in.a], error)) {
    if (error.find("not hashable") != std::string::npos) {
      return raise_exception_value(runtime.make_exception("TypeError", error))
                 ? XlangVMOpFlow::ContinueLoop
                 : XlangVMOpFlow::ReturnResult;
    }
    return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  return XlangVMOpFlow::Next;
}

template <typename RaiseRuntimeError>
XLANG3_HOT_INLINE XlangVMOpFlow set_update(
    const ir::Instr& in,
    XlangVMSmallRegisterBuffer& regs,
    RaiseRuntimeError&& raise_runtime_error) {
  std::string error;
  Value iterator;
  if (!sequence_get_iter(regs[in.a], iterator, error)) {
    return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  for (;;) {
    bool done = false;
    Value item;
    if (!sequence_iter_next(iterator, done, item, error)) {
      return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
    if (done) {
      break;
    }
    if (!::xlang3::set_add(regs[in.dst], item, error)) {
      return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
  }
  return XlangVMOpFlow::Next;
}

template <typename RaiseRuntimeError>
XLANG3_HOT_INLINE XlangVMOpFlow tuple_from_list(
    const ir::Instr& in,
    XlangVMSmallRegisterBuffer& regs,
    RaiseRuntimeError&& raise_runtime_error) {
  auto* list = value_as_list(regs[in.a]);
  if (list == nullptr) {
    return raise_runtime_error("tuple source is not a list") ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  std::vector<Value> items;
  items.reserve(list->items.size());
  for (const auto& item : list->items) {
    items.push_back(item);
  }
  regs[in.dst] = Value::tuple(std::move(items));
  return XlangVMOpFlow::Next;
}

template <typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow len(
    const ir::Instr& in,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    XlangVMInstrCache& cache,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  xlang_vm_cache_touch(cache, XlangVMCacheDomain::Len);
  if (regs[in.a].tag == ValueTag::Object && regs[in.a].as.obj != nullptr) {
    const ObjectKind kind = regs[in.a].as.obj->kind;
    if (cache.state == XlangVMCacheState::Specialized &&
        cache.specialization == XlangVMSpecializationId::LenObjectKind &&
        cache.object_kind == kind) {
      switch (kind) {
        case ObjectKind::List:
          value_set_int64(regs[in.dst], static_cast<int64_t>(reinterpret_cast<ListObject*>(regs[in.a].as.obj)->items.size()));
          xlang_vm_cache_note_hit(cache);
          return XlangVMOpFlow::Next;
        case ObjectKind::Tuple:
          value_set_int64(regs[in.dst], static_cast<int64_t>(reinterpret_cast<TupleObject*>(regs[in.a].as.obj)->items.size()));
          xlang_vm_cache_note_hit(cache);
          return XlangVMOpFlow::Next;
        case ObjectKind::String:
          value_set_int64(
              regs[in.dst],
              static_cast<int64_t>(utf8_codepoint_count(string_object_view(*reinterpret_cast<StringObject*>(regs[in.a].as.obj)))));
          xlang_vm_cache_note_hit(cache);
          return XlangVMOpFlow::Next;
        case ObjectKind::Bytes:
          value_set_int64(regs[in.dst], static_cast<int64_t>(reinterpret_cast<BytesObject*>(regs[in.a].as.obj)->size));
          xlang_vm_cache_note_hit(cache);
          return XlangVMOpFlow::Next;
        case ObjectKind::ByteArray:
          value_set_int64(regs[in.dst], static_cast<int64_t>(reinterpret_cast<ByteArrayObject*>(regs[in.a].as.obj)->value.size()));
          xlang_vm_cache_note_hit(cache);
          return XlangVMOpFlow::Next;
        case ObjectKind::MemoryView:
          value_set_int64(regs[in.dst], static_cast<int64_t>(reinterpret_cast<MemoryViewObject*>(regs[in.a].as.obj)->size));
          xlang_vm_cache_note_hit(cache);
          return XlangVMOpFlow::Next;
        case ObjectKind::Dict:
          value_set_int64(regs[in.dst], static_cast<int64_t>(reinterpret_cast<DictObject*>(regs[in.a].as.obj)->entries.size()));
          xlang_vm_cache_note_hit(cache);
          return XlangVMOpFlow::Next;
        case ObjectKind::Set:
          value_set_int64(regs[in.dst], static_cast<int64_t>(reinterpret_cast<SetObject*>(regs[in.a].as.obj)->items.size()));
          xlang_vm_cache_note_hit(cache);
          return XlangVMOpFlow::Next;
        default:
          xlang_vm_cache_deopt(cache);
          break;
      }
    } else if (cache.state == XlangVMCacheState::Specialized) {
      xlang_vm_cache_deopt(cache);
    }
    switch (kind) {
      case ObjectKind::List:
        value_set_int64(regs[in.dst], static_cast<int64_t>(value_as_list(regs[in.a])->items.size()));
        xlang_vm_cache_note_hit(cache);
        if (cache.state == XlangVMCacheState::Adaptive && cache.hit_count >= 8 && cache.miss_count == 0) {
          xlang_vm_cache_specialize(cache, XlangVMSpecializationId::LenObjectKind, kind);
        }
        return XlangVMOpFlow::Next;
      case ObjectKind::Tuple:
        value_set_int64(regs[in.dst], static_cast<int64_t>(value_as_tuple(regs[in.a])->items.size()));
        xlang_vm_cache_note_hit(cache);
        if (cache.state == XlangVMCacheState::Adaptive && cache.hit_count >= 8 && cache.miss_count == 0) {
          xlang_vm_cache_specialize(cache, XlangVMSpecializationId::LenObjectKind, kind);
        }
        return XlangVMOpFlow::Next;
      case ObjectKind::String:
        value_set_int64(regs[in.dst], static_cast<int64_t>(utf8_codepoint_count(string_object_view(*value_as_string(regs[in.a])))));
        xlang_vm_cache_note_hit(cache);
        if (cache.state == XlangVMCacheState::Adaptive && cache.hit_count >= 8 && cache.miss_count == 0) {
          xlang_vm_cache_specialize(cache, XlangVMSpecializationId::LenObjectKind, kind);
        }
        return XlangVMOpFlow::Next;
      case ObjectKind::Bytes:
        value_set_int64(regs[in.dst], static_cast<int64_t>(value_as_bytes(regs[in.a])->size));
        xlang_vm_cache_note_hit(cache);
        if (cache.state == XlangVMCacheState::Adaptive && cache.hit_count >= 8 && cache.miss_count == 0) {
          xlang_vm_cache_specialize(cache, XlangVMSpecializationId::LenObjectKind, kind);
        }
        return XlangVMOpFlow::Next;
      case ObjectKind::ByteArray:
        value_set_int64(regs[in.dst], static_cast<int64_t>(value_as_bytearray(regs[in.a])->value.size()));
        xlang_vm_cache_note_hit(cache);
        if (cache.state == XlangVMCacheState::Adaptive && cache.hit_count >= 8 && cache.miss_count == 0) {
          xlang_vm_cache_specialize(cache, XlangVMSpecializationId::LenObjectKind, kind);
        }
        return XlangVMOpFlow::Next;
      case ObjectKind::MemoryView:
        value_set_int64(regs[in.dst], static_cast<int64_t>(value_as_memoryview(regs[in.a])->size));
        xlang_vm_cache_note_hit(cache);
        if (cache.state == XlangVMCacheState::Adaptive && cache.hit_count >= 8 && cache.miss_count == 0) {
          xlang_vm_cache_specialize(cache, XlangVMSpecializationId::LenObjectKind, kind);
        }
        return XlangVMOpFlow::Next;
      case ObjectKind::Dict:
        value_set_int64(regs[in.dst], static_cast<int64_t>(value_as_dict(regs[in.a])->entries.size()));
        xlang_vm_cache_note_hit(cache);
        if (cache.state == XlangVMCacheState::Adaptive && cache.hit_count >= 8 && cache.miss_count == 0) {
          xlang_vm_cache_specialize(cache, XlangVMSpecializationId::LenObjectKind, kind);
        }
        return XlangVMOpFlow::Next;
      case ObjectKind::Set:
        value_set_int64(regs[in.dst], static_cast<int64_t>(value_as_set(regs[in.a])->items.size()));
        xlang_vm_cache_note_hit(cache);
        if (cache.state == XlangVMCacheState::Adaptive && cache.hit_count >= 8 && cache.miss_count == 0) {
          xlang_vm_cache_specialize(cache, XlangVMSpecializationId::LenObjectKind, kind);
        }
        return XlangVMOpFlow::Next;
      default:
        break;
    }
  }
  std::string error;
  if (!sequence_len(regs[in.a], regs[in.dst], error)) {
    if (value_as_instance(regs[in.a]) != nullptr) {
      Value len_method;
      std::string attr_error;
      if (object_get_attr(regs[in.a], "__len__", len_method, attr_error)) {
        if (runtime_call_callable(runtime, len_method, nullptr, 0, regs[in.dst], error)) {
          xlang_vm_cache_note_hit(cache);
          return XlangVMOpFlow::Next;
        }
        Value pending;
        if (runtime.take_pending_exception(pending)) {
          return raise_exception_value(std::move(pending)) ? XlangVMOpFlow::ContinueLoop
                                                           : XlangVMOpFlow::ReturnResult;
        }
      }
    }
    xlang_vm_cache_note_miss(cache);
    return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  xlang_vm_cache_note_hit(cache);
  return XlangVMOpFlow::Next;
}

XLANG3_HOT_INLINE void maybe_specialize_get_item_int(
    XlangVMInstrCache& cache,
    ObjectKind kind) {
  if (cache.state != XlangVMCacheState::Adaptive || cache.hit_count < 8 || cache.miss_count != 0) {
    return;
  }
  switch (kind) {
    case ObjectKind::List:
      xlang_vm_cache_specialize(cache, XlangVMSpecializationId::GetItemListInt, kind);
      break;
    case ObjectKind::Tuple:
      xlang_vm_cache_specialize(cache, XlangVMSpecializationId::GetItemTupleInt, kind);
      break;
    case ObjectKind::String:
      xlang_vm_cache_specialize(cache, XlangVMSpecializationId::GetItemStringInt, kind);
      break;
    case ObjectKind::Bytes:
      xlang_vm_cache_specialize(cache, XlangVMSpecializationId::GetItemBytesInt, kind);
      break;
    case ObjectKind::ByteArray:
      xlang_vm_cache_specialize(cache, XlangVMSpecializationId::GetItemByteArrayInt, kind);
      break;
    case ObjectKind::MemoryView:
      xlang_vm_cache_specialize(cache, XlangVMSpecializationId::GetItemMemoryViewInt, kind);
      break;
    default:
      break;
  }
}

template <typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow get_item(
    const ir::Instr& in,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    XlangVMInstrCache& cache,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  xlang_vm_cache_touch(cache, XlangVMCacheDomain::GetItem);
  if (regs[in.b].tag == ValueTag::Int64 && regs[in.a].tag == ValueTag::Object && regs[in.a].as.obj != nullptr) {
    const int64_t raw_index = regs[in.b].as.i64;
    Object* object = regs[in.a].as.obj;
    if (cache.state == XlangVMCacheState::Specialized && cache.object_kind == object->kind) {
      switch (cache.specialization) {
        case XlangVMSpecializationId::GetItemListInt: {
          auto* list = reinterpret_cast<ListObject*>(object);
          int64_t index = raw_index < 0 ? raw_index + static_cast<int64_t>(list->items.size()) : raw_index;
          if (index >= 0 && index < static_cast<int64_t>(list->items.size())) {
            value_borrow_assign_fast(regs[in.dst], list->items[static_cast<size_t>(index)]);
            xlang_vm_cache_note_hit(cache);
            return XlangVMOpFlow::Next;
          }
          break;
        }
        case XlangVMSpecializationId::GetItemTupleInt: {
          auto* tuple = reinterpret_cast<TupleObject*>(object);
          int64_t index = raw_index < 0 ? raw_index + static_cast<int64_t>(tuple->items.size()) : raw_index;
          if (index >= 0 && index < static_cast<int64_t>(tuple->items.size())) {
            value_borrow_assign_fast(regs[in.dst], tuple->items[static_cast<size_t>(index)]);
            xlang_vm_cache_note_hit(cache);
            return XlangVMOpFlow::Next;
          }
          break;
        }
        case XlangVMSpecializationId::GetItemStringInt: {
          auto* string = reinterpret_cast<StringObject*>(object);
          const auto view = string_object_view(*string);
          const auto codepoint_count = utf8_codepoint_count(view);
          int64_t index = raw_index < 0 ? raw_index + static_cast<int64_t>(codepoint_count) : raw_index;
          if (index >= 0 && index < static_cast<int64_t>(codepoint_count)) {
            regs[in.dst] = Value::string_view(utf8_codepoint_at(view, static_cast<size_t>(index)));
            xlang_vm_cache_note_hit(cache);
            return XlangVMOpFlow::Next;
          }
          break;
        }
        case XlangVMSpecializationId::GetItemBytesInt: {
          auto* bytes = reinterpret_cast<BytesObject*>(object);
          const auto view = bytes_object_view(*bytes);
          int64_t index = raw_index < 0 ? raw_index + static_cast<int64_t>(view.size()) : raw_index;
          if (index >= 0 && index < static_cast<int64_t>(view.size())) {
            value_set_int64(regs[in.dst], static_cast<unsigned char>(view[static_cast<size_t>(index)]));
            xlang_vm_cache_note_hit(cache);
            return XlangVMOpFlow::Next;
          }
          break;
        }
        case XlangVMSpecializationId::GetItemByteArrayInt: {
          auto* bytearray = reinterpret_cast<ByteArrayObject*>(object);
          int64_t index = raw_index < 0 ? raw_index + static_cast<int64_t>(bytearray->value.size()) : raw_index;
          if (index >= 0 && index < static_cast<int64_t>(bytearray->value.size())) {
            value_set_int64(regs[in.dst], static_cast<unsigned char>(bytearray->value[static_cast<size_t>(index)]));
            xlang_vm_cache_note_hit(cache);
            return XlangVMOpFlow::Next;
          }
          break;
        }
        default:
          break;
      }
    } else if (cache.state == XlangVMCacheState::Specialized) {
      xlang_vm_cache_deopt(cache);
    }
    if (object->kind == ObjectKind::List) {
      auto* list = value_as_list(regs[in.a]);
      int64_t index = raw_index < 0 ? raw_index + static_cast<int64_t>(list->items.size()) : raw_index;
      if (index >= 0 && index < static_cast<int64_t>(list->items.size())) {
        value_borrow_assign_fast(regs[in.dst], list->items[static_cast<size_t>(index)]);
        xlang_vm_cache_note_hit(cache);
        maybe_specialize_get_item_int(cache, object->kind);
        return XlangVMOpFlow::Next;
      }
    } else if (object->kind == ObjectKind::Tuple) {
      auto* tuple = value_as_tuple(regs[in.a]);
      int64_t index = raw_index < 0 ? raw_index + static_cast<int64_t>(tuple->items.size()) : raw_index;
      if (index >= 0 && index < static_cast<int64_t>(tuple->items.size())) {
        value_borrow_assign_fast(regs[in.dst], tuple->items[static_cast<size_t>(index)]);
        xlang_vm_cache_note_hit(cache);
        maybe_specialize_get_item_int(cache, object->kind);
        return XlangVMOpFlow::Next;
      }
    } else if (object->kind == ObjectKind::String) {
      auto* string = value_as_string(regs[in.a]);
      const auto view = string_object_view(*string);
      const auto codepoint_count = utf8_codepoint_count(view);
      int64_t index = raw_index < 0 ? raw_index + static_cast<int64_t>(codepoint_count) : raw_index;
      if (index >= 0 && index < static_cast<int64_t>(codepoint_count)) {
        regs[in.dst] = Value::string_view(utf8_codepoint_at(view, static_cast<size_t>(index)));
        xlang_vm_cache_note_hit(cache);
        maybe_specialize_get_item_int(cache, object->kind);
        return XlangVMOpFlow::Next;
      }
    } else if (object->kind == ObjectKind::Bytes) {
      auto* bytes = value_as_bytes(regs[in.a]);
      const auto view = bytes_object_view(*bytes);
      int64_t index = raw_index < 0 ? raw_index + static_cast<int64_t>(view.size()) : raw_index;
      if (index >= 0 && index < static_cast<int64_t>(view.size())) {
        value_set_int64(regs[in.dst], static_cast<unsigned char>(view[static_cast<size_t>(index)]));
        xlang_vm_cache_note_hit(cache);
        maybe_specialize_get_item_int(cache, object->kind);
        return XlangVMOpFlow::Next;
      }
    } else if (object->kind == ObjectKind::ByteArray) {
      auto* bytearray = value_as_bytearray(regs[in.a]);
      int64_t index = raw_index < 0 ? raw_index + static_cast<int64_t>(bytearray->value.size()) : raw_index;
      if (index >= 0 && index < static_cast<int64_t>(bytearray->value.size())) {
        value_set_int64(regs[in.dst], static_cast<unsigned char>(bytearray->value[static_cast<size_t>(index)]));
        xlang_vm_cache_note_hit(cache);
        maybe_specialize_get_item_int(cache, object->kind);
        return XlangVMOpFlow::Next;
      }
    } else if (object->kind == ObjectKind::MemoryView) {
      auto* view = value_as_memoryview(regs[in.a]);
      auto* owner = value_as_bytearray(view->owner);
      if (owner != nullptr) {
        int64_t index = raw_index < 0 ? raw_index + static_cast<int64_t>(view->size) : raw_index;
        if (index >= 0 && index < static_cast<int64_t>(view->size)) {
          value_set_int64(regs[in.dst], static_cast<unsigned char>(owner->value[view->offset + static_cast<size_t>(index)]));
          xlang_vm_cache_note_hit(cache);
          maybe_specialize_get_item_int(cache, object->kind);
          return XlangVMOpFlow::Next;
        }
      }
    }
  }
  std::string error;
  if (!sequence_get_item(regs[in.a], regs[in.b], regs[in.dst], error)) {
    xlang_vm_cache_note_miss(cache);
    if (value_as_instance(regs[in.a]) != nullptr) {
      Value getitem;
      std::string attr_error;
      if (object_get_attr(regs[in.a], "__getitem__", getitem, attr_error)) {
        const Value call_arg = regs[in.b];
        if (runtime_call_callable(runtime, getitem, &call_arg, 1, regs[in.dst], error)) {
          xlang_vm_cache_note_hit(cache);
          return XlangVMOpFlow::Next;
        }
      }
    }
    bool is_mapping_miss = error == "key not found" && value_as_dict(regs[in.a]) != nullptr;
    if (!is_mapping_miss) {
      if (auto* instance = value_as_instance(regs[in.a])) {
        is_mapping_miss = value_as_dict(instance->mapping_storage) != nullptr;
      }
    }
    if (is_mapping_miss) {
      return raise_exception_value(runtime.make_exception("KeyError", value_to_string(regs[in.b])))
                 ? XlangVMOpFlow::ContinueLoop
                 : XlangVMOpFlow::ReturnResult;
    }
    if (error == "index out of range") {
      return raise_exception_value(runtime.make_exception("IndexError", error)) ? XlangVMOpFlow::ContinueLoop
                                                                                : XlangVMOpFlow::ReturnResult;
    }
    return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  xlang_vm_cache_note_hit(cache);
  return XlangVMOpFlow::Next;
}

template <typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow set_item(
    const ir::Instr& in,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  if (regs[in.a].tag == ValueTag::Int64 && regs[in.b].tag == ValueTag::Int64 &&
      regs[in.b].as.i64 >= 0 && regs[in.b].as.i64 <= 255 &&
      regs[in.dst].tag == ValueTag::Object && regs[in.dst].as.obj != nullptr) {
    const int64_t raw_index = regs[in.a].as.i64;
    const auto byte = static_cast<char>(static_cast<unsigned char>(regs[in.b].as.i64));
    if (regs[in.dst].as.obj->kind == ObjectKind::ByteArray) {
      auto* bytearray = value_as_bytearray(regs[in.dst]);
      int64_t index = raw_index < 0 ? raw_index + static_cast<int64_t>(bytearray->value.size()) : raw_index;
      if (index >= 0 && index < static_cast<int64_t>(bytearray->value.size())) {
        bytearray->value[static_cast<size_t>(index)] = byte;
        return XlangVMOpFlow::Next;
      }
    } else if (regs[in.dst].as.obj->kind == ObjectKind::MemoryView) {
      auto* view = value_as_memoryview(regs[in.dst]);
      if (!view->readonly) {
        auto* owner = value_as_bytearray(view->owner);
        if (owner != nullptr) {
          int64_t index = raw_index < 0 ? raw_index + static_cast<int64_t>(view->size) : raw_index;
          if (index >= 0 && index < static_cast<int64_t>(view->size)) {
            owner->value[view->offset + static_cast<size_t>(index)] = byte;
            return XlangVMOpFlow::Next;
          }
        }
      }
    }
  }
  std::string error;
  if (!sequence_set_item(regs[in.dst], regs[in.a], regs[in.b], error)) {
    if (value_as_instance(regs[in.dst]) != nullptr) {
      Value setitem;
      std::string attr_error;
      if (object_get_attr(regs[in.dst], "__setitem__", setitem, attr_error)) {
        Value call_args[2] = {regs[in.a], regs[in.b]};
        Value ignored;
        if (runtime_call_callable(runtime, setitem, call_args, 2, ignored, error)) {
          return XlangVMOpFlow::Next;
        }
        Value pending;
        if (runtime.take_pending_exception(pending)) {
          return raise_exception_value(std::move(pending)) ? XlangVMOpFlow::ContinueLoop
                                                           : XlangVMOpFlow::ReturnResult;
        }
      }
    }
    return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  return XlangVMOpFlow::Next;
}

template <typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow delete_item(
    const ir::Instr& in,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  std::string error;
  if (!sequence_delete_item(regs[in.dst], regs[in.a], error)) {
    if (value_as_instance(regs[in.dst]) != nullptr) {
      Value delitem;
      std::string attr_error;
      if (object_get_attr(regs[in.dst], "__delitem__", delitem, attr_error)) {
        Value ignored;
        if (runtime_call_callable(runtime, delitem, &regs[in.a], 1, ignored, error)) {
          return XlangVMOpFlow::Next;
        }
        Value pending;
        if (runtime.take_pending_exception(pending)) {
          return raise_exception_value(std::move(pending)) ? XlangVMOpFlow::ContinueLoop
                                                           : XlangVMOpFlow::ReturnResult;
        }
      }
    }
    return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  return XlangVMOpFlow::Next;
}

template <typename RaiseRuntimeError>
XLANG3_HOT_INLINE XlangVMOpFlow unpack_sequence(
    const ir::Instr& in,
    XlangVMSmallRegisterBuffer& regs,
    RuntimeResult& result,
    RaiseRuntimeError&& raise_runtime_error) {
  const uint32_t first_output = in.dst;
  const uint32_t source = in.a;
  const uint32_t before_count = in.b;
  const bool has_star = (in.c & 0x80000000u) != 0;
  const uint32_t after_count = in.c & 0x7fffffffu;
  const uint32_t output_count = before_count + after_count + (has_star ? 1u : 0u);
  if (source >= regs.size() || first_output > regs.size() || output_count > regs.size() - first_output) {
    result.errors.push_back("invalid unpack registers");
    return XlangVMOpFlow::ReturnResult;
  }
  std::string error;
  Value iterator;
  if (!sequence_get_iter(regs[source], iterator, error)) {
    return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  std::vector<Value> values;
  for (;;) {
    bool done = false;
    Value item;
    if (!sequence_iter_next(iterator, done, item, error)) {
      return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
    if (done) break;
    values.push_back(std::move(item));
  }
  const size_t fixed_count = static_cast<size_t>(before_count) + static_cast<size_t>(after_count);
  if (!has_star && values.size() != fixed_count) {
    return raise_runtime_error("unpack expected " + std::to_string(fixed_count) + " values, got " + std::to_string(values.size()))
        ? XlangVMOpFlow::ContinueLoop
        : XlangVMOpFlow::ReturnResult;
  }
  if (has_star && values.size() < fixed_count) {
    return raise_runtime_error("unpack expected at least " + std::to_string(fixed_count) + " values, got " + std::to_string(values.size()))
        ? XlangVMOpFlow::ContinueLoop
        : XlangVMOpFlow::ReturnResult;
  }
  for (uint32_t i = 0; i < before_count; ++i) {
    value_assign_fast(regs[first_output + i], values[i]);
  }
  if (has_star) {
    std::vector<Value> rest;
    const size_t rest_begin = before_count;
    const size_t rest_end = values.size() - after_count;
    rest.reserve(rest_end - rest_begin);
    for (size_t i = rest_begin; i < rest_end; ++i) {
      rest.push_back(values[i]);
    }
    regs[first_output + before_count] = Value::list(std::move(rest));
    for (uint32_t i = 0; i < after_count; ++i) {
      value_assign_fast(regs[first_output + before_count + 1 + i], values[values.size() - after_count + i]);
    }
  }
  return XlangVMOpFlow::Next;
}

} // namespace xlang3::xlang_vm::ops
