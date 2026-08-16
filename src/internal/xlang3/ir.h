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

#include "xlang3/value.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace xlang3::ir {

enum class Op : uint16_t {
  LoadConst,
  LoadLocal,
  StoreLocal,
  MoveLocal,
  AddLocalConst,
  AddLocalLocal,
  LoadCell,
  StoreCell,
  LoadCellObject,
  LoadFree,
  StoreFree,
  LoadFreeObject,
  LoadGlobal,
  StoreGlobal,
  ImportModule,
  ImportFrom,
  RawBlock,
  LoadAttr,
  StoreAttr,
  LoadInstanceSlot,
  StoreInstanceSlot,
  MakeClass,
  MakeFunction,
  MakeTuple,
  MakeList,
  MakeDict,
  MakeSet,
  ListAppend,
  GetItem,
  SetItem,
  GetIter,
  IterNext,
  ForRangeConstLocalNext,
  Add,
  Sub,
  Mul,
  Div,
  BoolAnd,
  BoolOr,
  Compare,
  Not,
  Neg,
  Jump,
  JumpIfFalse,
  JumpIfLocalConstFalse,
  SetupExcept,
  SetupWith,
  PopExcept,
  Raise,
  Reraise,
  ClearException,
  LoadException,
  LoadExceptionType,
  CallMethod,
  Call,
  Pop,
  Return,
};

enum class CompareOp : uint16_t {
  Eq,
  Ne,
  Lt,
  Le,
  Gt,
  Ge,
};

struct Instr {
  Op op = Op::LoadConst;
  uint32_t dst = 0;
  uint32_t a = 0;
  uint32_t b = 0;
  uint32_t c = 0;
};

struct Function {
  std::string name;
  std::vector<std::string> params;
  std::vector<std::string> locals;
  std::vector<uint32_t> cell_slots;
  std::vector<std::string> free_vars;
  uint32_t register_count = 0;
  std::vector<Value> constants;
  std::vector<std::string> names;
  struct RawBlock {
    std::string language;
    std::string provider;
    std::string body;
  };
  std::vector<RawBlock> raw_blocks;
  std::vector<std::vector<uint32_t>> call_args;
  std::vector<std::vector<uint32_t>> tuple_items;
  std::vector<std::vector<uint32_t>> list_items;
  std::vector<std::vector<uint32_t>> set_items;
  std::vector<std::vector<std::pair<uint32_t, uint32_t>>> dict_items;
  std::vector<std::vector<uint32_t>> function_closures;
  std::vector<std::vector<std::pair<std::string, uint32_t>>> class_attrs;
  std::vector<std::vector<std::string>> class_instance_slots;
  std::vector<std::pair<uint32_t, uint32_t>> range_specs;
  std::vector<Instr> code;
};

struct Module {
  std::vector<Function> functions;
  uint32_t entry = 0;
};

std::string dump_module(const Module& module);

} // namespace xlang3::ir
