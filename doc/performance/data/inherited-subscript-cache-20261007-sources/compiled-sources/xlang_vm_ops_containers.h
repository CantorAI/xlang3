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
#include "xlang_vm_ops_variables.h"

#include "xlang3/functional_iterators.h"
#include "xlang3/mapping.h"
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"
#include "xlang3/set_object.h"
#include "xlang3/value_hash.h"

#include <algorithm>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace xlang3::xlang_vm::ops {

XLANG3_HOT_INLINE XlangVMOpFlow reverse_prefix_slice_assign(
    const ir::Instr& in,
    const ir::Function& fn,
    XlangVMFrame& frame,
    XlangVMSmallValueBuffer& locals,
    size_t& ip,
    bool allow_fast_path,
    RuntimeResult& result) {
  if (in.dst >= locals.size() || in.a >= locals.size()) {
    result.errors.push_back("invalid reverse-prefix slice local slot");
    return XlangVMOpFlow::ReturnResult;
  }
  const size_t fallback_span = in.c;
  if (fallback_span == 0 || ip >= fn.code.size() ||
      fallback_span >= fn.code.size() - ip) {
    result.errors.push_back("invalid reverse-prefix slice fallback span");
    return XlangVMOpFlow::ReturnResult;
  }

  const Value& sequence = locals[in.dst];
  const Value& index_value = locals[in.a];
  if (!allow_fast_path || index_value.tag != ValueTag::Int64 ||
      sequence.tag != ValueTag::Object || sequence.as.obj == nullptr ||
      sequence.as.obj->kind != ObjectKind::List ||
      index_value.as.i64 == std::numeric_limits<int64_t>::max()) {
    return XlangVMOpFlow::Next;
  }

  auto* list = reinterpret_cast<ListObject*>(sequence.as.obj);
  if (list->items.size() > static_cast<size_t>(std::numeric_limits<int64_t>::max())) {
    return XlangVMOpFlow::Next;
  }
  const int64_t length = static_cast<int64_t>(list->items.size());

  // For exact lists and exact ints, the two slices normalize to [0:stop] and
  // [start:-1:-1]. Only equal lengths denote a valid prefix reversal; other
  // inputs take the normal Python slice-assignment path and preserve errors.
  int64_t target_stop = index_value.as.i64 + 1;
  if (target_stop < 0) {
    target_stop += length;
    if (target_stop < 0) target_stop = 0;
  } else if (target_stop > length) {
    target_stop = length;
  }
  int64_t source_start = index_value.as.i64;
  if (source_start < 0) {
    source_start += length;
    if (source_start < 0) source_start = -1;
  } else if (source_start >= length) {
    source_start = length - 1;
  }
  const int64_t source_length = source_start < 0 ? 0 : source_start + 1;
  if (target_stop != source_length) return XlangVMOpFlow::Next;

  // CPython 3.14's generic STORE_SLICE path builds the reversed RHS list.
  // This guarded IR form proves that exact RHS is the destination's prefix in
  // reverse order, so reversing the existing Value slots avoids both slice
  // objects and the temporary list while leaving all other cases generic.
  std::reverse(list->items.begin(), list->items.begin() + target_stop);
  frame.release_registers_for_skipped_expression_fallback(fn, ip + 1, fallback_span);
  ip += fallback_span;
  return XlangVMOpFlow::Next;
}

XLANG3_HOT_INLINE XlangVMOpFlow guarded_local_list_get_item(
    const ir::Instr& in,
    const ir::Function& fn,
    XlangVMFrame& frame,
    XlangVMSmallRegisterBuffer& regs,
    XlangVMSmallValueBuffer& locals,
    size_t& ip,
    bool allow_fast_path,
    RuntimeResult& result) {
  const bool constant_index =
      (in.c & ir::kGuardedLocalListGetItemConstFlag) != 0;
  const size_t fallback_span = in.c & ir::kGuardedLocalListGetItemSpanMask;
  if (in.dst >= regs.size() || in.a >= locals.size() ||
      (constant_index ? in.b >= fn.constants.size() : in.b >= locals.size())) {
    result.errors.push_back("invalid guarded local list subscript operand");
    return XlangVMOpFlow::ReturnResult;
  }
  if (fallback_span == 0 || ip >= fn.code.size() ||
      fallback_span >= fn.code.size() - ip) {
    result.errors.push_back("invalid guarded local list subscript fallback span");
    return XlangVMOpFlow::ReturnResult;
  }

  const Value& sequence = locals[in.a];
  const Value& index = constant_index ? fn.constants[in.b] : locals[in.b];
  if (!allow_fast_path || index.tag != ValueTag::Int64 || index.as.i64 < 0 ||
      sequence.tag != ValueTag::Object || sequence.as.obj == nullptr ||
      sequence.as.obj->kind != ObjectKind::List) {
    return XlangVMOpFlow::Next;
  }
  auto* list = reinterpret_cast<ListObject*>(sequence.as.obj);
  const auto position = static_cast<uint64_t>(index.as.i64);
  if (position >= list->items.size()) return XlangVMOpFlow::Next;

  // CPython 3.14 specializes exact list + nonnegative exact int subscripts.
  // Read the local list directly, then skip register loads and generic GetItem
  // bytecode that remains as the type and bounds fallback.
  value_borrow_assign_fast(regs[in.dst], list->items[static_cast<size_t>(position)]);
  frame.release_registers_for_skipped_expression_fallback(
      fn, ip + 1, fallback_span, in.dst);
  ip += fallback_span;
  return XlangVMOpFlow::Next;
}

XLANG3_HOT_INLINE XlangVMOpFlow guarded_local_list_augment_const(
    const ir::Instr& in,
    const ir::Function& fn,
    XlangVMFrame& frame,
    XlangVMSmallValueBuffer& locals,
    size_t& ip,
    bool allow_fast_path,
    RuntimeResult& result) {
  const bool subtract = (in.c & ir::kGuardedLocalListAugmentSubtractFlag) != 0;
  const size_t fallback_span = in.c & ir::kGuardedLocalListAugmentSpanMask;
  if (in.dst >= locals.size() || in.a >= locals.size() || in.b >= fn.constants.size()) {
    result.errors.push_back("invalid guarded local list augmented assignment operand");
    return XlangVMOpFlow::ReturnResult;
  }
  if (fallback_span == 0 || ip >= fn.code.size() ||
      fallback_span >= fn.code.size() - ip) {
    result.errors.push_back("invalid guarded local list augmented assignment fallback span");
    return XlangVMOpFlow::ReturnResult;
  }

  const Value& sequence = locals[in.dst];
  const Value& index = locals[in.a];
  const Value& amount = fn.constants[in.b];
  if (!allow_fast_path || index.tag != ValueTag::Int64 || index.as.i64 < 0 ||
      amount.tag != ValueTag::Int64 || sequence.tag != ValueTag::Object ||
      sequence.as.obj == nullptr || sequence.as.obj->kind != ObjectKind::List) {
    return XlangVMOpFlow::Next;
  }
  auto* list = reinterpret_cast<ListObject*>(sequence.as.obj);
  const auto position = static_cast<uint64_t>(index.as.i64);
  if (position >= list->items.size()) return XlangVMOpFlow::Next;
  Value& current = list->items[static_cast<size_t>(position)];
  if (current.tag != ValueTag::Int64) return XlangVMOpFlow::Next;
  int64_t updated = 0;
  const bool fits = subtract
      ? xlang_vm_checked_sub_i64(current.as.i64, amount.as.i64, updated)
      : xlang_vm_checked_add_i64(current.as.i64, amount.as.i64, updated);
  if (!fits) return XlangVMOpFlow::Next;

  // CPython's list/int augmented-store path avoids general subscription and
  // arithmetic dispatch. Restrict this form to exact lists and exact ints;
  // overflow, subclasses, and overloaded arithmetic retain the normal IR.
  value_set_int64(current, updated);
  frame.release_registers_for_skipped_expression_fallback(fn, ip + 1, fallback_span);
  ip += fallback_span;
  return XlangVMOpFlow::Next;
}

XLANG3_HOT_INLINE XlangVMOpFlow while_reverse_prefix_count(
    const ir::Instr& in,
    const ir::Function& fn,
    XlangVMFrame& frame,
    XlangVMSmallValueBuffer& locals,
    size_t& ip,
    bool allow_fast_path,
    RuntimeResult& result) {
  if (in.dst >= locals.size() || in.a >= locals.size() || in.b >= locals.size()) {
    result.errors.push_back("invalid reverse-prefix loop local slot");
    return XlangVMOpFlow::ReturnResult;
  }
  const size_t fallback_span = in.c;
  if (fallback_span == 0 || ip >= fn.code.size() ||
      fallback_span >= fn.code.size() - ip) {
    result.errors.push_back("invalid reverse-prefix loop fallback span");
    return XlangVMOpFlow::ReturnResult;
  }

  const Value& sequence = locals[in.dst];
  const Value& index = locals[in.a];
  const Value& counter = locals[in.b];
  if (!allow_fast_path || index.tag != ValueTag::Int64 ||
      counter.tag != ValueTag::Int64 ||
      sequence.tag != ValueTag::Object || sequence.as.obj == nullptr ||
      sequence.as.obj->kind != ObjectKind::List) {
    return XlangVMOpFlow::Next;
  }
  auto* list = reinterpret_cast<ListObject*>(sequence.as.obj);
  if (list->items.size() > static_cast<size_t>(std::numeric_limits<int64_t>::max())) {
    return XlangVMOpFlow::Next;
  }
  const int64_t length = static_cast<int64_t>(list->items.size());
  int64_t flips = counter.as.i64;
  bool completed_iteration = false;
  size_t fused_iterations = 0;

  // The compiler emits this only for the exact three-statement loop body
  // `values[:index + 1] = values[index::-1]; count += 1; index = values[0]`.
  // CPython executes those as separate adaptive bytecodes and materializes a
  // reversed list; here exact-list/exact-int guards let one VM op run several
  // reversals. Any type, range, or overflow miss resumes the ordinary loop.
  for (;;) {
    // Bound each dispatch so cyclic user lists still visit the VM's event
    // polling boundary and can be interrupted just like the generic loop.
    if (fused_iterations >= 64 ||
        ((fused_iterations & 15u) == 0 && interpreter_pending_events() != 0)) {
      break;
    }
    const Value& current_index = locals[in.a];
    if (current_index.tag != ValueTag::Int64) {
      if (!completed_iteration) return XlangVMOpFlow::Next;
      break;
    }
    const int64_t k = current_index.as.i64;
    if (k == 0) {
      if (!completed_iteration) {
        ip += fallback_span;
        return XlangVMOpFlow::Next;
      }
      break;
    }
    if (k < 0 || k >= length || flips == std::numeric_limits<int64_t>::max()) {
      if (!completed_iteration) return XlangVMOpFlow::Next;
      break;
    }

    std::reverse(list->items.begin(), list->items.begin() + (k + 1));
    ++flips;
    ++fused_iterations;
    completed_iteration = true;
    value_assign_fast(locals[in.a], list->items.front());
  }

  if (completed_iteration) {
    value_set_int64(locals[in.b], flips);
    frame.release_registers_for_skipped_expression_fallback(fn, ip + 1, fallback_span);
    // Leave the loop's existing back-edge in place. It reevaluates the while
    // condition and either exits or resumes the generic body on a guard miss.
    ip += fallback_span;
  }
  return XlangVMOpFlow::Next;
}

XLANG3_HOT_INLINE XlangVMOpFlow list_pop_front_insert(
    const ir::Instr& in,
    const ir::Function& fn,
    XlangVMFrame& frame,
    XlangVMSmallValueBuffer& locals,
    size_t& ip,
    bool allow_fast_path,
    RuntimeResult& result) {
  if (in.dst >= locals.size() || in.a >= locals.size() || in.b >= locals.size()) {
    result.errors.push_back("invalid list pop-insert local slot");
    return XlangVMOpFlow::ReturnResult;
  }
  const size_t fallback_span = in.c;
  if (fallback_span == 0 || ip >= fn.code.size() ||
      fallback_span >= fn.code.size() - ip) {
    result.errors.push_back("invalid list pop-insert fallback span");
    return XlangVMOpFlow::ReturnResult;
  }
  if (!allow_fast_path) return XlangVMOpFlow::Next;

  const Value& insert_value = locals[in.a];
  const Value& pop_value = locals[in.b];
  const Value& index_value = locals[in.dst];
  auto* insert_method = value_as_bound_method(insert_value);
  auto* pop_method = value_as_bound_method(pop_value);
  if (insert_method == nullptr || pop_method == nullptr ||
      insert_method->self.tag != ValueTag::Object ||
      insert_method->self.as.obj == nullptr ||
      insert_method->self.as.obj != pop_method->self.as.obj ||
      index_value.tag != ValueTag::Int64 || index_value.as.i64 < 0) {
    return XlangVMOpFlow::Next;
  }
  auto* insert_function = value_as_native_function(insert_method->function);
  auto* pop_function = value_as_native_function(pop_method->function);
  if (insert_function == nullptr || pop_function == nullptr ||
      insert_function->specialization_id != kBuiltinMethodSpecializationListInsert ||
      pop_function->specialization_id != kBuiltinMethodSpecializationListPop) {
    return XlangVMOpFlow::Next;
  }
  auto* list = value_as_mutable_list_storage(insert_method->self);
  if (list == nullptr || list->items.empty() ||
      static_cast<uint64_t>(index_value.as.i64) >= list->items.size()) {
    return XlangVMOpFlow::Next;
  }

  // Removing item zero and reinserting it at `index` rotates exactly the
  // first index+1 elements left once. Guard builtin method identities rather
  // than local names; custom callables and mismatched lists keep ordinary IR.
  const size_t prefix_size = static_cast<size_t>(index_value.as.i64) + 1;
  std::rotate(list->items.begin(), list->items.begin() + 1,
              list->items.begin() + static_cast<std::ptrdiff_t>(prefix_size));
  frame.release_registers_for_skipped_expression_fallback(fn, ip + 1, fallback_span);
  ip += fallback_span;
  return XlangVMOpFlow::Next;
}

XLANG3_HOT_INLINE XlangVMOpFlow while_list_permutation_advance(
    const ir::Instr& in,
    const ir::Function& fn,
    XlangVMFrame& frame,
    XlangVMSmallValueBuffer& locals,
    size_t& ip,
    bool allow_fast_path,
    RuntimeResult& result) {
  if (in.dst >= locals.size() || in.a >= locals.size() || in.b >= locals.size() ||
      in.c >= fn.call_args.size() || fn.call_args[in.c].size() != 4 ||
      fn.call_args[in.c][0] >= locals.size() || fn.call_args[in.c][1] >= locals.size()) {
    result.errors.push_back("invalid list permutation loop operands");
    return XlangVMOpFlow::ReturnResult;
  }
  const auto& spec = fn.call_args[in.c];
  const size_t fallback_span = spec[2];
  const size_t else_span = spec[3];
  if (fallback_span == 0 || ip >= fn.code.size() ||
      fallback_span >= fn.code.size() - ip ||
      else_span >= fn.code.size() - ip - fallback_span) {
    result.errors.push_back("invalid list permutation loop fallback span");
    return XlangVMOpFlow::ReturnResult;
  }
  if (!allow_fast_path) return XlangVMOpFlow::Next;

  const Value& index_value = locals[in.dst];
  const Value& limit_value = locals[spec[1]];
  const Value& insert_value = locals[in.a];
  const Value& pop_value = locals[in.b];
  if (index_value.tag != ValueTag::Int64 || limit_value.tag != ValueTag::Int64) {
    return XlangVMOpFlow::Next;
  }
  auto* insert_method = value_as_bound_method(insert_value);
  auto* pop_method = value_as_bound_method(pop_value);
  if (insert_method == nullptr || pop_method == nullptr ||
      insert_method->self.tag != ValueTag::Object ||
      insert_method->self.as.obj == nullptr ||
      insert_method->self.as.obj != pop_method->self.as.obj) {
    return XlangVMOpFlow::Next;
  }
  auto* insert_function = value_as_native_function(insert_method->function);
  auto* pop_function = value_as_native_function(pop_method->function);
  if (insert_function == nullptr || pop_function == nullptr ||
      insert_function->specialization_id != kBuiltinMethodSpecializationListInsert ||
      pop_function->specialization_id != kBuiltinMethodSpecializationListPop) {
    return XlangVMOpFlow::Next;
  }
  // `value_as_list` deliberately excludes list subclasses, whose overridden
  // item or method behavior must run through the ordinary loop fallback.
  auto* permutation = value_as_list(insert_method->self);
  auto* counts = value_as_list(locals[spec[0]]);
  if (permutation == nullptr || counts == nullptr) return XlangVMOpFlow::Next;

  int64_t current = index_value.as.i64;
  const int64_t limit = limit_value.as.i64;
  size_t completed = 0;
  bool finished = false;
  bool loop_exhausted = false;
  for (;;) {
    if (current == limit) {
      finished = true;
      loop_exhausted = true;
      break;
    }
    // Limit each native batch and poll at the same short interval used by
    // other loop fusions, so cancellation and pending callbacks remain timely.
    if (completed >= 64 ||
        ((completed & 15u) == 0 && interpreter_pending_events() != 0)) {
      break;
    }
    if (current < 0 || static_cast<uint64_t>(current) >= permutation->items.size() ||
        static_cast<uint64_t>(current) >= counts->items.size()) {
      break;
    }
    Value& count = counts->items[static_cast<size_t>(current)];
    if (count.tag != ValueTag::Int64 || count.as.i64 == std::numeric_limits<int64_t>::min()) {
      break;
    }
    const int64_t updated_count = count.as.i64 - 1;
    const bool stop_on_positive_count = updated_count > 0;
    if (!stop_on_positive_count && current == std::numeric_limits<int64_t>::max()) {
      break;
    }

    // With exact list/method/int guards, pop(0)+insert(index, item) is a left
    // rotation of the prefix. The counter update and break test are folded in
    // too, replacing repeated Python-level loop dispatches with one VM op.
    const size_t prefix_size = static_cast<size_t>(current) + 1;
    std::rotate(permutation->items.begin(), permutation->items.begin() + 1,
                permutation->items.begin() + static_cast<std::ptrdiff_t>(prefix_size));
    value_set_int64(count, updated_count);
    ++completed;
    if (stop_on_positive_count) {
      finished = true;
      break;
    }
    ++current;
  }

  if (completed != 0) value_set_int64(locals[in.dst], current);
  if (finished) {
    const size_t skipped_span = fallback_span + (loop_exhausted ? 0 : else_span);
    frame.release_registers_for_skipped_expression_fallback(fn, ip + 1, skipped_span);
    ip += skipped_span;
  }
  // A partial batch resumes at the loop's condition. Guard misses therefore
  // execute the original CPython-compatible calls, subscriptions, and errors.
  return XlangVMOpFlow::Next;
}

XLANG3_HOT_INLINE XlangVMOpFlow while_reset_count(
    const ir::Instr& in,
    const ir::Function& fn,
    XlangVMFrame& frame,
    XlangVMSmallValueBuffer& locals,
    size_t& ip,
    bool allow_fast_path,
    RuntimeResult& result) {
  if (in.dst >= locals.size() || in.a >= locals.size()) {
    result.errors.push_back("invalid count-reset loop local slot");
    return XlangVMOpFlow::ReturnResult;
  }
  const size_t fallback_span = in.c;
  if (fallback_span == 0 || ip >= fn.code.size() ||
      fallback_span >= fn.code.size() - ip) {
    result.errors.push_back("invalid count-reset loop fallback span");
    return XlangVMOpFlow::ReturnResult;
  }
  if (!allow_fast_path || locals[in.dst].tag != ValueTag::Int64) {
    return XlangVMOpFlow::Next;
  }
  auto* counts = value_as_mutable_list_storage(locals[in.a]);
  if (counts == nullptr || counts->items.size() >
          static_cast<size_t>(std::numeric_limits<int64_t>::max())) {
    return XlangVMOpFlow::Next;
  }
  int64_t current = locals[in.dst].as.i64;
  const int64_t length = static_cast<int64_t>(counts->items.size());
  if (current <= 1 || current > length) return XlangVMOpFlow::Next;

  size_t completed = 0;
  while (current != 1 && completed < 64) {
    if ((completed & 15u) == 0 && interpreter_pending_events() != 0) {
      if (completed == 0) return XlangVMOpFlow::Next;
      break;
    }
    if (current <= 1 || current > length) {
      // The fast guard established a valid positive countdown. The check also
      // keeps future edits from turning malformed locals into unchecked access.
      break;
    }
    Value& item = counts->items[static_cast<size_t>(current - 1)];
    if (item.tag != ValueTag::Int64) {
      if (completed == 0) return XlangVMOpFlow::Next;
      break;
    }
    value_set_int64(item, current);
    --current;
    ++completed;
  }
  if (completed == 0) return XlangVMOpFlow::Next;

  // Exact int/list guards make each update equivalent to the generic indexed
  // store and decrement. The bounded batch preserves VM event polling for long
  // countdowns; type and bounds misses keep the original IR body available.
  value_set_int64(locals[in.dst], current);
  frame.release_registers_for_skipped_expression_fallback(fn, ip + 1, fallback_span);
  ip += fallback_span;
  return XlangVMOpFlow::Next;
}

XLANG3_HOT_INLINE XlangVMOpFlow guarded_local_list_compare(
    const ir::Instr& in,
    const ir::Function& fn,
    XlangVMSmallRegisterBuffer& regs,
    XlangVMFrame& frame,
    XlangVMSmallValueBuffer& locals,
    size_t& ip,
    bool allow_fast_path,
    RuntimeResult& result) {
  const bool index_constant = (in.c & ir::kGuardedLocalListCompareIndexConstFlag) != 0;
  const bool rhs_constant = (in.c & ir::kGuardedLocalListCompareRhsConstFlag) != 0;
  const bool branch = (in.c & ir::kGuardedLocalListCompareBranchFlag) != 0;
  const size_t fallback_span = in.c & ir::kGuardedLocalListCompareSpanMask;
  if (in.dst >= regs.size() || in.a >= locals.size() || in.b >= fn.call_args.size() ||
      fn.call_args[in.b].size() != (branch ? 4u : 3u) || fallback_span == 0 || ip >= fn.code.size() ||
      fallback_span >= fn.code.size() - ip ||
      (branch && (fallback_span + 1 >= fn.code.size() - ip ||
                  (fn.code[ip + fallback_span + 1].op != ir::Op::JumpIfFalse &&
                   fn.code[ip + fallback_span + 1].op != ir::Op::MoveJumpIfFalse) ||
                  fn.code[ip + fallback_span + 1].a != in.dst ||
                  (fn.code[ip + fallback_span + 1].op == ir::Op::JumpIfFalse
                       ? fn.code[ip + fallback_span + 1].dst
                       : fn.code[ip + fallback_span + 1].b) != fn.call_args[in.b][3] ||
                  (fn.code[ip + fallback_span + 1].op == ir::Op::MoveJumpIfFalse &&
                   fn.code[ip + fallback_span + 1].dst >= regs.size())))) {
    result.errors.push_back("invalid guarded list comparison operands");
    return XlangVMOpFlow::ReturnResult;
  }
  if (!allow_fast_path) return XlangVMOpFlow::Next;

  const auto& spec = fn.call_args[in.b];
  const uint32_t index_operand = spec[0];
  const uint32_t rhs_operand = spec[1];
  if ((index_constant ? index_operand >= fn.constants.size() : index_operand >= locals.size()) ||
      (rhs_constant ? rhs_operand >= fn.constants.size() : rhs_operand >= locals.size()) ||
      spec[2] > static_cast<uint32_t>(ir::CompareOp::Ge) ||
      (branch && spec[3] > fn.code.size())) {
    result.errors.push_back("invalid guarded list comparison spec");
    return XlangVMOpFlow::ReturnResult;
  }

  const Value& index = index_constant ? fn.constants[index_operand] : locals[index_operand];
  const Value& rhs = rhs_constant ? fn.constants[rhs_operand] : locals[rhs_operand];
  auto* list = value_as_mutable_list_storage(locals[in.a]);
  if (list == nullptr || index.tag != ValueTag::Int64 || index.as.i64 < 0 ||
      static_cast<uint64_t>(index.as.i64) >= list->items.size()) {
    return XlangVMOpFlow::Next;
  }
  const Value& lhs = list->items[static_cast<size_t>(index.as.i64)];
  if (lhs.tag != ValueTag::Int64 || rhs.tag != ValueTag::Int64) {
    return XlangVMOpFlow::Next;
  }

  bool value = false;
  switch (static_cast<ir::CompareOp>(spec[2])) {
    case ir::CompareOp::Eq: value = lhs.as.i64 == rhs.as.i64; break;
    case ir::CompareOp::Ne: value = lhs.as.i64 != rhs.as.i64; break;
    case ir::CompareOp::Lt: value = lhs.as.i64 < rhs.as.i64; break;
    case ir::CompareOp::Le: value = lhs.as.i64 <= rhs.as.i64; break;
    case ir::CompareOp::Gt: value = lhs.as.i64 > rhs.as.i64; break;
    case ir::CompareOp::Ge: value = lhs.as.i64 >= rhs.as.i64; break;
  }

  // CPython specializes exact-list/integer subscripting and integer compares.
  // Fuse those two operations at the local load site; bools, subclasses,
  // negative indices, and custom values retain the ordinary comparison IR.
  value_set_bool(regs[in.dst], value);
  frame.release_registers_for_skipped_expression_fallback(
      fn, ip + 1, fallback_span, in.dst);
  if (branch) {
    const auto& branch_instr = fn.code[ip + fallback_span + 1];
    if (branch_instr.op == ir::Op::MoveJumpIfFalse) {
      // Short-circuit `and` returns its left operand on both paths, so the
      // fused branch must perform MoveJumpIfFalse's move before branching.
      value_assign_fast(regs[branch_instr.dst], regs[in.dst]);
    }
    if (!value) {
      ip = spec[3];
      return XlangVMOpFlow::ContinueLoop;
    }
    // The adjacent branch is the fallback for this comparison result. A
    // successful exact-list/int guard already computed it, including the
    // short-circuit destination used by MoveJumpIfFalse.
    ip += fallback_span + 1;
    return XlangVMOpFlow::Next;
  }
  ip += fallback_span;
  return XlangVMOpFlow::Next;
}

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
  auto assign_entry = [&](const Value& key, const Value& value, std::string& error) -> bool {
    return mapping_set_item_runtime(runtime, regs[in.dst], key, value, error);
  };
  for (const auto& pair : fn.dict_items[in.a]) {
    if (pair.second >= regs.size() || (pair.first != UINT32_MAX && pair.first >= regs.size())) {
      result.errors.push_back("invalid dict item register");
      return XlangVMOpFlow::ReturnResult;
    }
    std::string error;
    if (pair.first == UINT32_MAX) {
      Value iterator;
      if (!runtime_get_iter(runtime, regs[pair.second], iterator, error)) {
        return raise_exception_value(runtime.make_exception("TypeError", error))
                   ? XlangVMOpFlow::ContinueLoop
                   : XlangVMOpFlow::ReturnResult;
      }
      while (true) {
        bool done = false;
        Value key;
        if (!sequence_iter_next(iterator, done, key, error)) {
          return raise_exception_value(runtime.make_exception("TypeError", error))
                     ? XlangVMOpFlow::ContinueLoop
                     : XlangVMOpFlow::ReturnResult;
        }
        if (done) {
          break;
        }
        Value value;
        if (!mapping_get_item_runtime(runtime, regs[pair.second], key, value, error) ||
            !assign_entry(key, value, error)) {
          Value pending;
          if (runtime.take_pending_exception(pending)) {
            return raise_exception_value(std::move(pending))
                       ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
          }
          return raise_exception_value(runtime.make_exception("TypeError", error))
                     ? XlangVMOpFlow::ContinueLoop
                     : XlangVMOpFlow::ReturnResult;
        }
      }
      continue;
    }
    if (!assign_entry(regs[pair.first], regs[pair.second], error)) {
      Value pending;
      if (runtime.take_pending_exception(pending)) {
        return raise_exception_value(std::move(pending))
                   ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
      }
      return raise_exception_value(runtime.make_exception("TypeError", error))
                 ? XlangVMOpFlow::ContinueLoop
                 : XlangVMOpFlow::ReturnResult;
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
    if (!::xlang3::set_add_runtime(runtime, regs[in.dst], regs[reg], error)) {
      Value pending;
      if (runtime.take_pending_exception(pending)) {
        return raise_exception_value(std::move(pending))
                   ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
      }
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
    if (!error.empty()) {
      error += ": " + value_to_repr(regs[in.dst]);
    }
    return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  return XlangVMOpFlow::Next;
}

template <typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow list_extend(
    const ir::Instr& in,
    XlangVMSmallRegisterBuffer& regs,
    Runtime& runtime,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  std::string error;
  Value iterator;
  if (!runtime_get_iter(runtime, regs[in.a], iterator, error)) {
    Value pending;
    if (runtime.take_pending_exception(pending)) {
      return raise_exception_value(std::move(pending)) ? XlangVMOpFlow::ContinueLoop
                                                       : XlangVMOpFlow::ReturnResult;
    }
    return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  for (;;) {
    bool done = false;
    Value item;
    if (!sequence_iter_next(iterator, done, item, error)) {
      Value pending;
      if (runtime.take_pending_exception(pending)) {
        return raise_exception_value(std::move(pending)) ? XlangVMOpFlow::ContinueLoop
                                                         : XlangVMOpFlow::ReturnResult;
      }
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
  if (value_as_instance(regs[in.dst]) != nullptr) {
    Value setitem;
    std::string attr_error;
    if (object_get_class_attr_for_instance(regs[in.dst], "__setitem__", setitem, attr_error)) {
      if (auto* native = value_as_native_function(setitem);
          native == nullptr || native->name != "dict.__setitem__") {
        Value call_args[3] = {regs[in.dst], regs[in.a], regs[in.b]};
        Value ignored;
        std::string call_error;
        if (runtime_call_callable(runtime, setitem, call_args, 3, ignored, call_error)) {
          return XlangVMOpFlow::Next;
        }
        Value pending;
        if (runtime.take_pending_exception(pending)) {
          return raise_exception_value(std::move(pending)) ? XlangVMOpFlow::ContinueLoop
                                                           : XlangVMOpFlow::ReturnResult;
        }
        return raise_runtime_error(call_error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
      }
    }
  }
  std::string error;
  const bool set_ok = value_as_dict(regs[in.dst]) != nullptr
      ? mapping_set_item_runtime(runtime, regs[in.dst], regs[in.a], regs[in.b], error)
      : sequence_set_item(regs[in.dst], regs[in.a], regs[in.b], error);
  if (!set_ok) {
    Value pending;
    if (runtime.take_pending_exception(pending)) {
      return raise_exception_value(std::move(pending))
                 ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
    if (error.find("not hashable") != std::string::npos ||
        error.find("unhashable type") != std::string::npos ||
        error.find("does not support item assignment") != std::string::npos) {
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
        size_t hash = 0;
        std::string hash_error;
        if (!value_hash_key(item, hash, hash_error)) {
          return raise_runtime_error(hash_error)
              ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
        }
        set->items.push_back(item);
        set->item_hashes.push_back(hash);
      }
      return XlangVMOpFlow::Next;
    }
  }
  std::string error;
  if (!::xlang3::set_add_runtime(runtime, regs[in.dst], regs[in.a], error)) {
    Value pending;
    if (runtime.take_pending_exception(pending)) {
      return raise_exception_value(std::move(pending))
                 ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
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
XLANG3_HOT_INLINE XlangVMOpFlow set_update(
    const ir::Instr& in,
    XlangVMSmallRegisterBuffer& regs,
    Runtime& runtime,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  std::string error;
  Value iterator;
  if (!runtime_get_iter(runtime, regs[in.a], iterator, error)) {
    Value pending;
    if (runtime.take_pending_exception(pending)) {
      return raise_exception_value(std::move(pending)) ? XlangVMOpFlow::ContinueLoop
                                                       : XlangVMOpFlow::ReturnResult;
    }
    return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  for (;;) {
    bool done = false;
    Value item;
    if (!sequence_iter_next(iterator, done, item, error)) {
      Value pending;
      if (runtime.take_pending_exception(pending)) {
        return raise_exception_value(std::move(pending)) ? XlangVMOpFlow::ContinueLoop
                                                         : XlangVMOpFlow::ReturnResult;
      }
      return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
    if (done) {
      break;
    }
    if (!::xlang3::set_add_runtime(runtime, regs[in.dst], item, error)) {
      Value pending;
      if (runtime.take_pending_exception(pending)) {
        return raise_exception_value(std::move(pending))
                   ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
      }
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
XLANG3_HOT_INLINE XlangVMOpFlow len_unprofiled(
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
          {
          auto* string = reinterpret_cast<StringObject*>(regs[in.a].as.obj);
          value_set_int64(
              regs[in.dst],
              static_cast<int64_t>(string_object_length(*string)));
          xlang_vm_cache_note_hit(cache);
          return XlangVMOpFlow::Next;
          }
        case ObjectKind::Bytes:
          value_set_int64(regs[in.dst], static_cast<int64_t>(reinterpret_cast<BytesObject*>(regs[in.a].as.obj)->size));
          xlang_vm_cache_note_hit(cache);
          return XlangVMOpFlow::Next;
        case ObjectKind::ByteArray:
          value_set_int64(regs[in.dst], static_cast<int64_t>(reinterpret_cast<ByteArrayObject*>(regs[in.a].as.obj)->value.size()));
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
        {
        auto* string = value_as_string(regs[in.a]);
        value_set_int64(regs[in.dst], static_cast<int64_t>(string_object_length(*string)));
        xlang_vm_cache_note_hit(cache);
        if (cache.state == XlangVMCacheState::Adaptive && cache.hit_count >= 8 && cache.miss_count == 0) {
          xlang_vm_cache_specialize(cache, XlangVMSpecializationId::LenObjectKind, kind);
        }
        return XlangVMOpFlow::Next;
        }
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
    if (error == "operation forbidden on released memoryview object") {
      return raise_exception_value(runtime.make_exception("ValueError", error))
          ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
    Value len_method;
    std::string attr_error;
    if (attribute_get(regs[in.a], "__len__", len_method, attr_error)) {
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
    xlang_vm_cache_note_miss(cache);
    return raise_exception_value(runtime.make_exception("TypeError", error))
        ? XlangVMOpFlow::ContinueLoop
        : XlangVMOpFlow::ReturnResult;
  }
  xlang_vm_cache_note_hit(cache);
  return XlangVMOpFlow::Next;
}

template <typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow dict_set_const(
    const ir::Instr& in,
    const ir::Function& fn,
    XlangVMSmallRegisterBuffer& regs,
    RuntimeResult& result,
    Runtime& runtime,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  if (in.b >= fn.constants.size() || in.c >= regs.size()) {
    result.errors.push_back("invalid constant index");
    return XlangVMOpFlow::ReturnResult;
  }
  value_assign_fast(regs[in.c], fn.constants[in.b]);
  const ir::Instr set_in{ir::Op::DictSet, in.dst, in.a, in.c, 0};
  return dict_set(
      set_in, regs, runtime,
      std::forward<RaiseRuntimeError>(raise_runtime_error),
      std::forward<RaiseExceptionValue>(raise_exception_value));
}

template <typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow len(
    const ir::Instr& in,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    XlangVMInstrCache& cache,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  const Value* callable = runtime.find_builtin("len");
  const bool profiling = callable != nullptr &&
      runtime.profile_function().tag != ValueTag::Invalid &&
      runtime.profile_function().tag != ValueTag::None &&
      !runtime.profile_dispatch_active();
  Value profile_frame;
  std::string profile_error;
  if (profiling) {
    profile_frame = runtime.current_frame_snapshot();
    if (!runtime.emit_profile_event_for_frame(profile_frame, "c_call", *callable, profile_error)) {
      return raise_runtime_error(profile_error.empty() ? "profile callback failed" : profile_error)
          ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
  }
  const XlangVMOpFlow flow = len_unprofiled(
      in, runtime, regs, cache, raise_runtime_error, raise_exception_value);
  if (profiling) {
    const char* event_name = flow == XlangVMOpFlow::Next ? "c_return" : "c_exception";
    if (!runtime.emit_profile_event_for_frame(profile_frame, event_name, *callable, profile_error)) {
      return raise_runtime_error(profile_error.empty() ? "profile callback failed" : profile_error)
          ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
  }
  return flow;
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

template <typename MakeGeneratorIfNeeded, typename PushFrame,
          typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow get_item(
    const ir::Instr& in,
    const ir::Module& module,
    const std::shared_ptr<const ir::Module>& module_owner,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    XlangVMInstrCache& cache,
    size_t& ip,
    RuntimeResult& result,
    MakeGeneratorIfNeeded&& make_generator_if_needed,
    PushFrame&& push_frame,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  xlang_vm_cache_touch(cache, XlangVMCacheDomain::GetItem);
  if (value_as_dict(regs[in.a]) != nullptr &&
      regs[in.b].tag == ValueTag::Int64 &&
      mapping_get_integer_item_if_present(
          regs[in.a], regs[in.b].as.i64, regs[in.dst])) {
    xlang_vm_cache_note_hit(cache);
    return XlangVMOpFlow::Next;
  }
  if (value_as_dict(regs[in.a]) != nullptr && value_as_string(regs[in.b]) != nullptr) {
    std::string error;
    if (mapping_get_item(regs[in.a], regs[in.b], regs[in.dst], error)) {
      xlang_vm_cache_note_hit(cache);
      return XlangVMOpFlow::Next;
    }
  }
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
          const auto codepoint_count = string_object_length(*string);
          int64_t index = raw_index < 0 ? raw_index + static_cast<int64_t>(codepoint_count) : raw_index;
          if (index >= 0 && index < static_cast<int64_t>(codepoint_count)) {
            const auto item = string_object_codepoint_at(*string, static_cast<size_t>(index));
            regs[in.dst] = Value::string_view(item);
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
      const auto codepoint_count = string_object_length(*string);
      int64_t index = raw_index < 0 ? raw_index + static_cast<int64_t>(codepoint_count) : raw_index;
      if (index >= 0 && index < static_cast<int64_t>(codepoint_count)) {
        const auto item = string_object_codepoint_at(*string, static_cast<size_t>(index));
        regs[in.dst] = Value::string_view(item);
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
      const auto storage = memoryview_object_view(*view);
      if (storage.data() != nullptr && view->format == "B") {
        int64_t index = raw_index < 0 ? raw_index + static_cast<int64_t>(view->size) : raw_index;
        if (index >= 0 && index < static_cast<int64_t>(view->size)) {
          value_set_int64(regs[in.dst], static_cast<unsigned char>(storage[static_cast<size_t>(index)]));
          xlang_vm_cache_note_hit(cache);
          maybe_specialize_get_item_int(cache, object->kind);
          return XlangVMOpFlow::Next;
        }
      }
    }
  }
  std::string error;
  if (auto* instance = value_as_instance(regs[in.a])) {
    auto* klass = value_as_class(instance->klass);
    if (klass != nullptr) {
      auto& method_cache = cache.call;
      FunctionObject* getitem = nullptr;
      NativeFunctionObject* native_getitem = nullptr;
      if (method_cache.kind == CallSiteKind::GetItemUserFunction &&
          method_cache.callee_object == &klass->header &&
          method_cache.class_version == klass->version) {
        getitem = method_cache.function;
      } else if (method_cache.kind == CallSiteKind::GetItemNativeFunction &&
                 method_cache.callee_object == &klass->header &&
                 method_cache.class_version == klass->version) {
        native_getitem = method_cache.native;
      } else {
        Value inherited_method;
        std::string lookup_error;
        const Value* method = nullptr;
        const auto direct_method = klass->attrs.find("__getitem__");
        if (direct_method != klass->attrs.end()) {
          method = &direct_method->second;
        } else if (object_get_class_attr_for_instance(
                       regs[in.a], "__getitem__", inherited_method, lookup_error)) {
          method = &inherited_method;
        }
        // Inherited Python/native methods use the same call path as direct
        // methods. Counter previously allocated a bound method and argument
        // vector for every read. Cache only raw functions, never descriptors;
        // base mutations invalidate subclass version tags before decref.
        getitem = method == nullptr ? nullptr : value_as_function(*method);
        if (getitem != nullptr) {
          // Special-method lookup binds a plain class function to the object.
          // Pass self and the index straight to the normal Python frame path,
          // avoiding a temporary bound-method object and generic callable
          // dispatch while leaving the Python method body authoritative.
          method_cache.callee_object = &klass->header;
          method_cache.kind = CallSiteKind::GetItemUserFunction;
          method_cache.function = getitem;
          method_cache.native = nullptr;
          method_cache.class_version = klass->version;
        } else if (method != nullptr) {
          native_getitem = value_as_native_function(*method);
          if (native_getitem != nullptr && native_getitem->bind_as_descriptor &&
              !native_getitem->capture_expressions && native_getitem->callback != nullptr) {
            method_cache.callee_object = &klass->header;
            method_cache.kind = CallSiteKind::GetItemNativeFunction;
            method_cache.function = nullptr;
            method_cache.native = native_getitem;
            method_cache.class_version = klass->version;
          } else {
            native_getitem = nullptr;
          }
        }
      }
      if (getitem != nullptr) {
        Value method_arguments[2];
        value_borrow_assign_fast(method_arguments[0], regs[in.a]);
        value_borrow_assign_fast(method_arguments[1], regs[in.b]);
        CallArgsView method_args;
        method_args.leading = method_arguments;
        method_args.leading_count = 2;
        bool pushed_frame = false;
        if (!call_user_function(
                getitem, method_args, module, module_owner, in.dst, ip,
                regs[in.dst], pushed_frame, make_generator_if_needed,
                push_frame)) {
          if (!result.errors.empty()) return XlangVMOpFlow::ReturnResult;
          return XlangVMOpFlow::ContinueLoop;
        }
        xlang_vm_cache_note_hit(cache);
        return pushed_frame ? XlangVMOpFlow::SwitchFrame : XlangVMOpFlow::Next;
      }
      if (native_getitem != nullptr) {
        // Own the callable and operands during callbacks: __index__ may mutate
        // the class or this instruction's output may alias its source. Cache
        // only weak targets guarded by the process-wide class version, avoiding
        // persistent roots and per-read bound-method allocations.
        Value callable;
        callable.tag = ValueTag::Object;
        callable.as.obj = &native_getitem->header;
        retain(callable);
        const Value arguments[2] = {regs[in.a], regs[in.b]};
        if (runtime_call_callable(runtime, callable, arguments, 2, regs[in.dst], error)) {
          xlang_vm_cache_note_hit(cache);
          return XlangVMOpFlow::Next;
        }
        Value pending;
        if (runtime.take_pending_exception(pending)) {
          return raise_exception_value(std::move(pending)) ? XlangVMOpFlow::ContinueLoop
                                                         : XlangVMOpFlow::ReturnResult;
        }
        return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
      }
    }
  }
  if (auto* subscribed_class = value_as_class(regs[in.a])) {
    Value metaclass_getitem;
    std::string metaclass_error;
    if (value_as_class(subscribed_class->metaclass) != nullptr &&
        object_get_attr(subscribed_class->metaclass, "__getitem__", metaclass_getitem, metaclass_error)) {
      if (value_as_function(metaclass_getitem) != nullptr ||
          value_as_native_function(metaclass_getitem) != nullptr) {
        metaclass_getitem = Value::bound_method(regs[in.a], std::move(metaclass_getitem));
      }
      const Value call_arg = regs[in.b];
      if (runtime_call_callable(runtime, metaclass_getitem, &call_arg, 1, regs[in.dst], error)) {
        xlang_vm_cache_note_hit(cache);
        return XlangVMOpFlow::Next;
      }
      Value pending;
      if (runtime.take_pending_exception(pending)) {
        return raise_exception_value(std::move(pending)) ? XlangVMOpFlow::ContinueLoop
                                                         : XlangVMOpFlow::ReturnResult;
      }
      return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop
                                        : XlangVMOpFlow::ReturnResult;
    }
    Value class_getitem;
    std::string attr_error;
    if (object_get_attr(regs[in.a], "__class_getitem__", class_getitem, attr_error)) {
      if (value_as_function(class_getitem) != nullptr) {
        class_getitem = Value::bound_method(regs[in.a], std::move(class_getitem));
      }
      const Value call_arg = regs[in.b];
      if (runtime_call_callable(runtime, class_getitem, &call_arg, 1, regs[in.dst], error)) {
        xlang_vm_cache_note_hit(cache);
        return XlangVMOpFlow::Next;
      }
      Value pending;
      if (runtime.take_pending_exception(pending)) {
        return raise_exception_value(std::move(pending)) ? XlangVMOpFlow::ContinueLoop
                                                         : XlangVMOpFlow::ReturnResult;
      }
      return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop
                                        : XlangVMOpFlow::ReturnResult;
    }
  }
  if (value_as_mapping_proxy(regs[in.a]) != nullptr) {
    Value getitem;
    std::string attr_error;
    if (attribute_get(regs[in.a], "__getitem__", getitem, attr_error)) {
      const Value call_arg = regs[in.b];
      if (runtime_call_callable(runtime, getitem, &call_arg, 1, regs[in.dst], error)) {
        xlang_vm_cache_note_hit(cache);
        return XlangVMOpFlow::Next;
      }
      Value pending;
      if (runtime.take_pending_exception(pending)) {
        return raise_exception_value(std::move(pending)) ? XlangVMOpFlow::ContinueLoop
                                                         : XlangVMOpFlow::ReturnResult;
      }
      return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop
                                        : XlangVMOpFlow::ReturnResult;
    }
  }
  if (value_as_instance(regs[in.a]) != nullptr) {
    Value class_getitem;
    std::string class_attr_error;
    if (object_get_class_attr_for_instance(regs[in.a], "__getitem__", class_getitem, class_attr_error) &&
        (value_as_function(class_getitem) != nullptr ||
         value_as_native_function(class_getitem) != nullptr ||
         object_value_has_descriptor_get(class_getitem))) {
      Value getitem;
      if (object_get_special_method(runtime, regs[in.a], "__getitem__", getitem, class_attr_error)) {
        const Value call_arg = regs[in.b];
        if (runtime_call_callable(runtime, getitem, &call_arg, 1, regs[in.dst], error)) {
          xlang_vm_cache_note_hit(cache);
          return XlangVMOpFlow::Next;
        }
        Value pending;
        if (runtime.take_pending_exception(pending)) {
          return raise_exception_value(std::move(pending)) ? XlangVMOpFlow::ContinueLoop
                                                           : XlangVMOpFlow::ReturnResult;
        }
        return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop
                                          : XlangVMOpFlow::ReturnResult;
      }
    }
  }
  const auto* mapping_instance = value_as_instance(regs[in.a]);
  const bool runtime_mapping = value_as_dict(regs[in.a]) != nullptr ||
      (mapping_instance != nullptr &&
       value_as_class(mapping_instance->klass) != nullptr &&
       class_has_builtin_base_name(value_as_class(mapping_instance->klass), "dict") &&
       value_as_dict(mapping_instance->mapping_storage) != nullptr);
  const bool mapping_found = runtime_mapping &&
      mapping_get_item_runtime(runtime, regs[in.a], regs[in.b], regs[in.dst], error);
  if (mapping_found) {
    xlang_vm_cache_note_hit(cache);
    return XlangVMOpFlow::Next;
  }
  if (!runtime_mapping && !sequence_get_item(regs[in.a], regs[in.b], regs[in.dst], error, &runtime)) {
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
        Value pending;
        if (runtime.take_pending_exception(pending)) {
          return raise_exception_value(std::move(pending)) ? XlangVMOpFlow::ContinueLoop
                                                           : XlangVMOpFlow::ReturnResult;
        }
      }
    }
    Value pending;
    if (runtime.take_pending_exception(pending)) {
      return raise_exception_value(std::move(pending)) ? XlangVMOpFlow::ContinueLoop
                                                       : XlangVMOpFlow::ReturnResult;
    }
    bool is_mapping_miss = error == "key not found" &&
        (value_as_dict(regs[in.a]) != nullptr || value_as_mapping_proxy(regs[in.a]) != nullptr ||
         value_as_module(regs[in.a]) != nullptr);
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
    if (error.find("index out of range") != std::string::npos) {
      const std::string message = value_as_tuple(regs[in.a]) != nullptr
          ? "tuple index out of range" : error;
      return raise_exception_value(runtime.make_exception("IndexError", message)) ? XlangVMOpFlow::ContinueLoop
                                                                                  : XlangVMOpFlow::ReturnResult;
    }
    if (error == "sequence index must be int") {
      return raise_exception_value(runtime.make_exception("TypeError", error)) ? XlangVMOpFlow::ContinueLoop
                                                                               : XlangVMOpFlow::ReturnResult;
    }
    if (error == "slice step cannot be zero") {
      return raise_exception_value(runtime.make_exception("ValueError", error))
          ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
    if (error == "object is not subscriptable") {
      std::string message = error;
      if (auto* klass = value_as_class(regs[in.a])) {
        message = "type '" + klass->name + "' is not subscriptable";
      } else if (value_as_function(regs[in.a]) != nullptr) {
        message = "'function' object is not subscriptable";
      } else if (value_as_native_function(regs[in.a]) != nullptr) {
        message = "'builtin_function_or_method' object is not subscriptable";
      }
      return raise_exception_value(runtime.make_exception("TypeError", message))
                 ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
    if (error == "operation forbidden on released memoryview object") {
      return raise_exception_value(runtime.make_exception("ValueError", error))
          ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
    return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  if (runtime_mapping) {
    if (error == "key not found") {
      return raise_exception_value(runtime.make_exception("KeyError", value_to_string(regs[in.b])))
                 ? XlangVMOpFlow::ContinueLoop
                 : XlangVMOpFlow::ReturnResult;
    }
    if (error.find("not hashable") != std::string::npos ||
        error.find("unhashable type") != std::string::npos) {
      return raise_exception_value(runtime.make_exception("TypeError", error))
                 ? XlangVMOpFlow::ContinueLoop
                 : XlangVMOpFlow::ReturnResult;
    }
    Value pending;
    if (runtime.take_pending_exception(pending)) {
      return raise_exception_value(std::move(pending)) ? XlangVMOpFlow::ContinueLoop
                                                       : XlangVMOpFlow::ReturnResult;
    }
    return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  xlang_vm_cache_note_hit(cache);
  return XlangVMOpFlow::Next;
}

template <typename MakeGeneratorIfNeeded, typename PushFrame, typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow set_item(
    const ir::Instr& in,
    const ir::Module& module,
    const std::shared_ptr<const ir::Module>& module_owner,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    XlangVMInstrCacheStorage& caches,
    size_t& ip,
    RuntimeResult& result,
    MakeGeneratorIfNeeded&& make_generator_if_needed,
    PushFrame&& push_frame,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  std::string& error = xlang_vm_native_error_scratch();
  error.clear();
  auto raise_set_item_error = [&]() -> XlangVMOpFlow {
    const std::string& mapped_error = error;
    if (mapped_error == "operation forbidden on released memoryview object") {
      return raise_exception_value(runtime.make_exception("ValueError", mapped_error))
          ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
    if (mapped_error == "Existing exports of data: object cannot be re-sized") {
      return raise_exception_value(runtime.make_exception("BufferError", mapped_error))
          ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
    bool is_mapping_miss = mapped_error == "key not found" && value_as_dict(regs[in.dst]) != nullptr;
    if (!is_mapping_miss) {
      if (auto* instance = value_as_instance(regs[in.dst])) {
        is_mapping_miss = value_as_dict(instance->mapping_storage) != nullptr;
      }
    }
    if (is_mapping_miss) {
      return raise_exception_value(runtime.make_exception("KeyError", value_to_string(regs[in.a])))
                 ? XlangVMOpFlow::ContinueLoop
                 : XlangVMOpFlow::ReturnResult;
    }
    if (mapped_error == "index out of range") {
      return raise_exception_value(runtime.make_exception("IndexError", mapped_error)) ? XlangVMOpFlow::ContinueLoop
                                                                                       : XlangVMOpFlow::ReturnResult;
    }
    if (mapped_error.find("does not support item assignment") != std::string::npos ||
        mapped_error.find("not hashable") != std::string::npos ||
        mapped_error.find("unhashable type") != std::string::npos) {
      return raise_exception_value(runtime.make_exception("TypeError", mapped_error))
                 ? XlangVMOpFlow::ContinueLoop
                 : XlangVMOpFlow::ReturnResult;
    }
    return raise_runtime_error(mapped_error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  };

  if (value_as_dict(regs[in.dst]) != nullptr && value_as_instance(regs[in.a]) == nullptr) {
    if (mapping_set_item(regs[in.dst], regs[in.a], regs[in.b], error)) {
      return XlangVMOpFlow::Next;
    }
    return raise_set_item_error();
  }

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
      if (view->format == "B") {
        auto* storage = memoryview_object_writable_data(*view);
        if (storage != nullptr) {
          int64_t index = raw_index < 0 ? raw_index + static_cast<int64_t>(view->size) : raw_index;
          if (index >= 0 && index < static_cast<int64_t>(view->size)) {
            storage[static_cast<size_t>(index)] = byte;
            return XlangVMOpFlow::Next;
          }
        }
      }
    }
  }
  if (value_as_instance(regs[in.dst]) != nullptr) {
    auto* instance = value_as_instance(regs[in.dst]);
    auto* klass = value_as_class(instance->klass);
    FunctionObject* python_setitem = nullptr;
    NativeFunctionObject* native_setitem = nullptr;
    XlangVMInstrCache* cache = nullptr;
    if (klass != nullptr) {
      // Primitive container writes do not use this cache. Fetch its sparse
      // payload only after the receiver is proven to need method dispatch.
      cache = &caches[ip];
      xlang_vm_cache_touch(*cache, XlangVMCacheDomain::Call);
      auto& method_cache = cache->call;
      if (method_cache.callee_object == &klass->header &&
          method_cache.class_version == klass->version &&
          method_cache.kind == CallSiteKind::SetItemUserFunction) {
        python_setitem = method_cache.function;
      } else if (method_cache.callee_object == &klass->header &&
                 method_cache.class_version == klass->version &&
                 method_cache.kind == CallSiteKind::SetItemNativeFunction) {
        native_setitem = method_cache.native;
      } else {
        Value inherited_method;
        std::string lookup_error;
        const Value* method = nullptr;
        const auto direct_method = klass->attrs.find("__setitem__");
        if (direct_method != klass->attrs.end()) {
          method = &direct_method->second;
        } else if (object_get_class_attr_for_instance(
                       regs[in.dst], "__setitem__", inherited_method, lookup_error)) {
          method = &inherited_method;
        }
        // Reuse the guarded setter entry for inherited methods as well. Keep
        // wrappers/custom descriptors on their normal binding path, and keep
        // cache targets non-owning so finished methods are not persistent roots.
        if (method != nullptr) {
          python_setitem = value_as_function(*method);
          native_setitem = value_as_native_function(*method);
          if (native_setitem != nullptr && (!native_setitem->bind_as_descriptor ||
              native_setitem->capture_expressions || native_setitem->callback == nullptr))
            native_setitem = nullptr;
          if (python_setitem != nullptr || native_setitem != nullptr) {
            method_cache.callee_object = &klass->header;
            method_cache.class_version = klass->version;
            method_cache.kind = python_setitem != nullptr ? CallSiteKind::SetItemUserFunction
                                                        : CallSiteKind::SetItemNativeFunction;
            method_cache.function = python_setitem;
            method_cache.native = native_setitem;
          }
        }
      }
    }
    if (python_setitem != nullptr) {
      // Keep Python setters in the caller's VM frame stack, as getters already
      // are. Generic runtime re-entry more than doubled a simple setter's cost.
      // Normal frame binding, tracing and exception propagation stay active;
      // the frame return mode discards the result without clobbering self.
      Value arguments[3];
      value_borrow_assign_fast(arguments[0], regs[in.dst]);
      value_borrow_assign_fast(arguments[1], regs[in.a]);
      value_borrow_assign_fast(arguments[2], regs[in.b]);
      CallArgsView args;
      args.leading = arguments;
      args.leading_count = 3;
      Value ignored;
      bool pushed_frame = false;
      if (!call_user_function(python_setitem, args, module, module_owner, in.dst, ip,
                              ignored, pushed_frame, make_generator_if_needed, push_frame,
                              FrameReturnMode::DiscardReturnValue)) {
        return !result.errors.empty() ? XlangVMOpFlow::ReturnResult : XlangVMOpFlow::ContinueLoop;
      }
      xlang_vm_cache_note_hit(*cache);
      return pushed_frame ? XlangVMOpFlow::SwitchFrame : XlangVMOpFlow::Next;
    }
    if (native_setitem != nullptr) {
      Value callable;
      callable.tag = ValueTag::Object;
      callable.as.obj = &native_setitem->header;
      retain(callable);
      const Value arguments[3] = {regs[in.dst], regs[in.a], regs[in.b]};
      Value ignored;
      if (runtime_call_callable(runtime, callable, arguments, 3, ignored, error)) {
        xlang_vm_cache_note_hit(*cache);
        return XlangVMOpFlow::Next;
      }
      Value pending;
      if (runtime.take_pending_exception(pending)) {
        return raise_exception_value(std::move(pending)) ? XlangVMOpFlow::ContinueLoop
                                                       : XlangVMOpFlow::ReturnResult;
      }
      return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
    Value setitem;
    std::string attr_error;
    if (object_get_special_method(runtime, regs[in.dst], "__setitem__", setitem, attr_error)) {
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
      return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
    Value pending;
    if (runtime.take_pending_exception(pending)) {
      return raise_exception_value(std::move(pending)) ? XlangVMOpFlow::ContinueLoop
                                                     : XlangVMOpFlow::ReturnResult;
    }
  }
  const bool set_ok = value_as_dict(regs[in.dst]) != nullptr
      ? mapping_set_item_runtime(runtime, regs[in.dst], regs[in.a], regs[in.b], error)
      : sequence_set_item(regs[in.dst], regs[in.a], regs[in.b], error);
  if (!set_ok) {
    Value pending;
    if (runtime.take_pending_exception(pending)) {
      return raise_exception_value(std::move(pending))
                 ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
    if (value_as_instance(regs[in.dst]) != nullptr) {
      Value setitem;
      std::string attr_error;
      if (object_get_special_method(runtime, regs[in.dst], "__setitem__", setitem, attr_error)) {
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
    return raise_set_item_error();
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
  Value target = regs[in.dst];
  // Subscription deletion is special-method dispatch, including for subclasses
  // that use native dict/list storage.  Dispatch before touching that storage so
  // overrides such as collections.OrderedDict.__delitem__ can maintain their
  // companion state.  An explicit dict.__delitem__(obj, key) still reaches the
  // native dict method and intentionally bypasses the override.
  if (value_as_instance(target) != nullptr) {
    Value delitem;
    std::string attr_error;
    if (object_get_attr(target, "__delitem__", delitem, attr_error)) {
      Value ignored;
      if (runtime_call_callable(runtime, delitem, &regs[in.a], 1, ignored, error)) {
        return XlangVMOpFlow::Next;
      }
      Value pending;
      if (runtime.take_pending_exception(pending)) {
        return raise_exception_value(std::move(pending)) ? XlangVMOpFlow::ContinueLoop
                                                         : XlangVMOpFlow::ReturnResult;
      }
      return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop
                                        : XlangVMOpFlow::ReturnResult;
    }
  }
  const bool runtime_mapping = value_as_dict(target) != nullptr ||
      (value_as_instance(target) != nullptr &&
       value_as_dict(value_as_instance(target)->mapping_storage) != nullptr);
  const bool deleted = runtime_mapping
      ? mapping_delete_item_runtime(runtime, target, regs[in.a], error)
      : sequence_delete_item(target, regs[in.a], error);
  if (!deleted) {
    if (error == "Existing exports of data: object cannot be re-sized") {
      return raise_exception_value(runtime.make_exception("BufferError", error))
          ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
    if (error == "key not found") {
      return raise_exception_value(runtime.make_exception("KeyError", value_to_repr(regs[in.a])))
          ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
    return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  return XlangVMOpFlow::Next;
}

template <typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow unpack_sequence(
    const ir::Instr& in,
    XlangVMSmallRegisterBuffer& regs,
    Runtime& runtime,
    RuntimeResult& result,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
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
  // Exact built-in tuples and lists cannot override iteration.  Unpack them
  // directly, as CPython's specialized UNPACK_SEQUENCE paths do, instead of
  // constructing an iterator and a temporary vector for every assignment.
  const Value* direct_values = nullptr;
  size_t direct_size = 0;
  bool has_direct_values = false;
  if (auto* tuple = value_as_tuple(regs[source])) {
    direct_values = tuple->items.begin();
    direct_size = tuple->items.size();
    has_direct_values = true;
  } else if (auto* list = value_as_list(regs[source])) {
    direct_values = list->items.data();
    direct_size = list->items.size();
    has_direct_values = true;
  }
  if (has_direct_values) {
    const size_t fixed_count = static_cast<size_t>(before_count) + static_cast<size_t>(after_count);
    if ((!has_star && direct_size != fixed_count) ||
        (has_star && direct_size < fixed_count)) {
      const std::string message = direct_size < fixed_count
          ? (has_star
              ? "not enough values to unpack (expected at least " + std::to_string(fixed_count) +
                    ", got " + std::to_string(direct_size) + ")"
              : "not enough values to unpack (expected " + std::to_string(fixed_count) +
                    ", got " + std::to_string(direct_size) + ")")
          : "too many values to unpack (expected " + std::to_string(fixed_count) + ")";
      return raise_exception_value(runtime.make_exception("ValueError", message))
          ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
    for (uint32_t i = 0; i < before_count; ++i) {
      value_assign_fast(regs[first_output + i], direct_values[i]);
    }
    if (has_star) {
      std::vector<Value> rest;
      const size_t rest_begin = before_count;
      const size_t rest_end = direct_size - after_count;
      rest.reserve(rest_end - rest_begin);
      for (size_t i = rest_begin; i < rest_end; ++i) {
        rest.push_back(direct_values[i]);
      }
      regs[first_output + before_count] = Value::list(std::move(rest));
      for (uint32_t i = 0; i < after_count; ++i) {
        value_assign_fast(
            regs[first_output + before_count + 1 + i],
            direct_values[direct_size - after_count + i]);
      }
    }
    return XlangVMOpFlow::Next;
  }
  std::string error;
  Value iterator;
  // Tuple/list subclasses may override __iter__.  Use the runtime protocol
  // entry point so destructuring observes that override just like for-loops
  // and tuple(iterable) do.
  if (!runtime_get_iter(runtime, regs[source], iterator, error)) {
    Value pending;
    if (runtime.take_pending_exception(pending)) {
      return raise_exception_value(std::move(pending)) ? XlangVMOpFlow::ContinueLoop
                                                       : XlangVMOpFlow::ReturnResult;
    }
    return raise_exception_value(runtime.make_exception("TypeError", "cannot unpack non-iterable object"))
        ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  std::vector<Value> values;
  for (;;) {
    bool done = false;
    Value item;
    if (!sequence_iter_next(iterator, done, item, error)) {
      Value pending;
      if (runtime.take_pending_exception(pending)) {
        return raise_exception_value(std::move(pending)) ? XlangVMOpFlow::ContinueLoop
                                                         : XlangVMOpFlow::ReturnResult;
      }
      return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
    if (done) break;
    values.push_back(std::move(item));
  }
  const size_t fixed_count = static_cast<size_t>(before_count) + static_cast<size_t>(after_count);
  if (!has_star && values.size() != fixed_count) {
    const std::string message = values.size() < fixed_count
        ? "not enough values to unpack (expected " + std::to_string(fixed_count) + ", got " + std::to_string(values.size()) + ")"
        : "too many values to unpack (expected " + std::to_string(fixed_count) + ")";
    return raise_exception_value(runtime.make_exception("ValueError", message))
        ? XlangVMOpFlow::ContinueLoop
        : XlangVMOpFlow::ReturnResult;
  }
  if (has_star && values.size() < fixed_count) {
    const std::string message = "not enough values to unpack (expected at least " +
        std::to_string(fixed_count) + ", got " + std::to_string(values.size()) + ")";
    return raise_exception_value(runtime.make_exception("ValueError", message))
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

template <typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow unpack_sequence_or_local_pair(
    const ir::Instr& in,
    const ir::Function& fn,
    XlangVMSmallRegisterBuffer& regs,
    XlangVMSmallValueBuffer& locals,
    Runtime& runtime,
    RuntimeResult& result,
    size_t& ip,
    bool collapse_unobservable_pair,
    const std::vector<size_t>& register_last_use,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  const auto flow = unpack_sequence(
      in, regs, runtime, result,
      std::forward<RaiseRuntimeError>(raise_runtime_error),
      std::forward<RaiseExceptionValue>(raise_exception_value));
  if (flow != XlangVMOpFlow::Next || !collapse_unobservable_pair ||
      ip + 1 >= fn.code.size()) {
    return flow;
  }
  const auto& store = fn.code[ip + 1];
  if (store.op != ir::Op::StoreLocalPair ||
      store.a != in.dst || store.c != in.dst + 1) {
    return flow;
  }
  store_local_pair(store, regs, locals, register_last_use, ip + 1);
  ++ip;
  return XlangVMOpFlow::Next;
}

} // namespace xlang3::xlang_vm::ops
