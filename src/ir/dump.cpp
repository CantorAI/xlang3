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
#include "xlang3/ir.h"

#include <sstream>

namespace xlang3::ir {

namespace {

const char* op_name(Op op) {
  switch (op) {
    case Op::LoadConst: return "LoadConst";
    case Op::LoadLocal: return "LoadLocal";
    case Op::StoreLocal: return "StoreLocal";
    case Op::MoveLocal: return "MoveLocal";
    case Op::AddLocalConst: return "AddLocalConst";
    case Op::AddLocalLocal: return "AddLocalLocal";
    case Op::LoadCell: return "LoadCell";
    case Op::StoreCell: return "StoreCell";
    case Op::LoadCellObject: return "LoadCellObject";
    case Op::LoadFree: return "LoadFree";
    case Op::StoreFree: return "StoreFree";
    case Op::LoadFreeObject: return "LoadFreeObject";
    case Op::LoadGlobal: return "LoadGlobal";
    case Op::StoreGlobal: return "StoreGlobal";
    case Op::ImportModule: return "ImportModule";
    case Op::ImportFrom: return "ImportFrom";
    case Op::RawBlock: return "RawBlock";
    case Op::LoadAttr: return "LoadAttr";
    case Op::StoreAttr: return "StoreAttr";
    case Op::LoadInstanceSlot: return "LoadInstanceSlot";
    case Op::StoreInstanceSlot: return "StoreInstanceSlot";
    case Op::MakeClass: return "MakeClass";
    case Op::MakeFunction: return "MakeFunction";
    case Op::MakeTuple: return "MakeTuple";
    case Op::MakeList: return "MakeList";
    case Op::MakeDict: return "MakeDict";
    case Op::MakeSet: return "MakeSet";
    case Op::ListAppend: return "ListAppend";
    case Op::GetItem: return "GetItem";
    case Op::SetItem: return "SetItem";
    case Op::GetIter: return "GetIter";
    case Op::IterNext: return "IterNext";
    case Op::ForRangeConstLocalNext: return "ForRangeConstLocalNext";
    case Op::Add: return "Add";
    case Op::Sub: return "Sub";
    case Op::Mul: return "Mul";
    case Op::Div: return "Div";
    case Op::BoolAnd: return "BoolAnd";
    case Op::BoolOr: return "BoolOr";
    case Op::Compare: return "Compare";
    case Op::Not: return "Not";
    case Op::Neg: return "Neg";
    case Op::Jump: return "Jump";
    case Op::JumpIfFalse: return "JumpIfFalse";
    case Op::JumpIfLocalConstFalse: return "JumpIfLocalConstFalse";
    case Op::SetupExcept: return "SetupExcept";
    case Op::PopExcept: return "PopExcept";
    case Op::Raise: return "Raise";
    case Op::CallMethod: return "CallMethod";
    case Op::Call: return "Call";
    case Op::Pop: return "Pop";
    case Op::Return: return "Return";
  }
  return "Unknown";
}

const char* compare_name(CompareOp op) {
  switch (op) {
    case CompareOp::Eq: return "Eq";
    case CompareOp::Ne: return "Ne";
    case CompareOp::Lt: return "Lt";
    case CompareOp::Le: return "Le";
    case CompareOp::Gt: return "Gt";
    case CompareOp::Ge: return "Ge";
  }
  return "Unknown";
}

} // namespace

std::string dump_module(const Module& module) {
  std::ostringstream os;
  os << "entry: #" << module.entry << "\n\n";
  for (size_t fn_i = 0; fn_i < module.functions.size(); ++fn_i) {
    const auto& fn = module.functions[fn_i];
    os << "function #" << fn_i << " " << fn.name << "\n";
    os << "  params:";
    for (size_t i = 0; i < fn.params.size(); ++i) {
      os << " %" << i << "=" << fn.params[i];
    }
    os << "\n";
    os << "  locals:";
    for (size_t i = 0; i < fn.locals.size(); ++i) {
      os << " %" << i << "=" << fn.locals[i];
    }
    os << "\n";
    os << "  cells:";
    for (auto slot : fn.cell_slots) {
      os << " %" << slot;
    }
    os << "\n";
    os << "  free_vars:";
    for (size_t i = 0; i < fn.free_vars.size(); ++i) {
      os << " $" << i << "=" << fn.free_vars[i];
    }
    os << "\n";
    os << "  registers: " << fn.register_count << "\n";
    os << "  constants: " << fn.constants.size() << "\n";
    os << "  names:";
    for (size_t i = 0; i < fn.names.size(); ++i) {
      os << " #" << i << "=" << fn.names[i];
    }
    os << "\n";
    for (size_t args_i = 0; args_i < fn.call_args.size(); ++args_i) {
      os << "  call_args #" << args_i << ":";
      for (auto reg : fn.call_args[args_i]) {
        os << " r" << reg;
      }
      os << "\n";
    }
    for (size_t raw_i = 0; raw_i < fn.raw_blocks.size(); ++raw_i) {
      os << "  raw_block #" << raw_i << ": "
         << fn.raw_blocks[raw_i].language
         << " " << fn.raw_blocks[raw_i].provider
         << " bytes=" << fn.raw_blocks[raw_i].body.size() << "\n";
    }
    for (size_t tuple_i = 0; tuple_i < fn.tuple_items.size(); ++tuple_i) {
      os << "  tuple_items #" << tuple_i << ":";
      for (auto reg : fn.tuple_items[tuple_i]) {
        os << " r" << reg;
      }
      os << "\n";
    }
    for (size_t list_i = 0; list_i < fn.list_items.size(); ++list_i) {
      os << "  list_items #" << list_i << ":";
      for (auto reg : fn.list_items[list_i]) {
        os << " r" << reg;
      }
      os << "\n";
    }
    for (size_t set_i = 0; set_i < fn.set_items.size(); ++set_i) {
      os << "  set_items #" << set_i << ":";
      for (auto reg : fn.set_items[set_i]) {
        os << " r" << reg;
      }
      os << "\n";
    }
    for (size_t dict_i = 0; dict_i < fn.dict_items.size(); ++dict_i) {
      os << "  dict_items #" << dict_i << ":";
      for (const auto& pair : fn.dict_items[dict_i]) {
        os << " (r" << pair.first << ": r" << pair.second << ")";
      }
      os << "\n";
    }
    for (size_t closure_i = 0; closure_i < fn.function_closures.size(); ++closure_i) {
      os << "  function_closure #" << closure_i << ":";
      for (auto reg : fn.function_closures[closure_i]) {
        os << " r" << reg;
      }
      os << "\n";
    }
    for (size_t attrs_i = 0; attrs_i < fn.class_attrs.size(); ++attrs_i) {
      os << "  class_attrs #" << attrs_i << ":";
      for (const auto& attr : fn.class_attrs[attrs_i]) {
        os << " " << attr.first << "=r" << attr.second;
      }
      os << "\n";
    }
    for (size_t slots_i = 0; slots_i < fn.class_instance_slots.size(); ++slots_i) {
      os << "  class_instance_slots #" << slots_i << ":";
      for (size_t slot_i = 0; slot_i < fn.class_instance_slots[slots_i].size(); ++slot_i) {
        os << " %" << slot_i << "=" << fn.class_instance_slots[slots_i][slot_i];
      }
      os << "\n";
    }
    for (size_t range_i = 0; range_i < fn.range_specs.size(); ++range_i) {
      os << "  range_spec #" << range_i
         << ": stop=c" << fn.range_specs[range_i].first
         << " step=c" << fn.range_specs[range_i].second << "\n";
    }
    for (size_t ip = 0; ip < fn.code.size(); ++ip) {
      const auto& in = fn.code[ip];
      os << "  " << ip << ": " << op_name(in.op)
         << " dst=" << in.dst << " a=" << in.a << " b=" << in.b << " c=" << in.c << "\n";
      if (in.op == Op::Compare) {
        os << "       compare=" << compare_name(static_cast<CompareOp>(in.c)) << "\n";
      }
    }
    os << "\n";
  }
  return os.str();
}

} // namespace xlang3::ir
