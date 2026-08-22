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

enum class ParamKind : uint8_t {
  PosOnly,
  PosOrKeyword,
  VarArgs,
  KeywordOnly,
  KwArgs,
};

struct Param {
  std::string name;
  ParamKind kind = ParamKind::PosOrKeyword;
  uint32_t default_reg = UINT32_MAX;
};

struct CallKeywordArg {
  std::string name;
  uint32_t value_reg = 0;
};

struct CallSpec {
  std::vector<uint32_t> positional;
  std::vector<CallKeywordArg> keywords;
  uint32_t star_arg = UINT32_MAX;
  uint32_t kw_star_arg = UINT32_MAX;
};

enum class Op : uint16_t {
  LoadConst,
  Move,
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
  DeleteLocal,
  DeleteGlobal,
  DeleteModuleSlot,
  LoadModuleSlot,
  StoreModuleSlot,
  ImportModule,
  ImportFrom,
  ImportStar,
  RawBlock,
  LoadAttr,
  StoreAttr,
  DeleteAttr,
  LoadInstanceSlot,
  StoreInstanceSlot,
  MakeClass,
  MakeFunction,
  SetFunctionAnnotations,
  SetFunctionKwDefaults,
  SetClassBase,
  MakeTuple,
  MakeList,
  MakeDict,
  MakeSet,
  MakeSlice,
  ListAppend,
  ListExtend,
  DictSet,
  SetAdd,
  SetUpdate,
  TupleFromList,
  Len,
  GetItem,
  SetItem,
  DeleteItem,
  UnpackSequence,
  GetIter,
  IterNext,
  ForRangeConstLocalNext,
  Add,
  Sub,
  Mul,
  Div,
  FloorDiv,
  Mod,
  ModConst,
  Pow,
  BitAnd,
  BitOr,
  BitXor,
  Shl,
  Shr,
  BoolAnd,
  BoolOr,
  Compare,
  Is,
  Contains,
  Not,
  Neg,
  Invert,
  Jump,
  JumpIfFalse,
  JumpIfLocalConstFalse,
  SetupExcept,
  SetupWith,
  PopExcept,
  Raise,
  SetExceptionCause,
  Reraise,
  ClearException,
  LoadException,
  LoadExceptionType,
  MatchException,
  CallModuleMethod,
  CallMethod,
  CallEx,
  Call,
  Await,
  Yield,
  YieldFrom,
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
  std::string qualname;
  std::vector<std::string> type_params;
  bool is_generator = false;
  bool is_async = false;
  uint32_t first_line = 0;
  std::vector<std::string> params;
  std::vector<Param> signature;
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
  std::vector<CallSpec> call_specs;
  std::vector<std::vector<uint32_t>> function_defaults;
  std::vector<std::vector<std::pair<std::string, uint32_t>>> function_annotations;
  std::vector<std::vector<std::pair<std::string, uint32_t>>> function_kwdefaults;
  std::vector<std::vector<uint32_t>> tuple_items;
  std::vector<std::vector<uint32_t>> list_items;
  std::vector<std::vector<uint32_t>> set_items;
  std::vector<std::vector<std::pair<uint32_t, uint32_t>>> dict_items;
  std::vector<std::vector<uint32_t>> function_closures;
  std::vector<std::vector<std::pair<std::string, uint32_t>>> class_attrs;
  std::vector<std::vector<std::string>> class_instance_slots;
  std::vector<std::pair<uint32_t, uint32_t>> range_specs;
  std::vector<std::pair<uint32_t, uint32_t>> string_replace_specs;
  std::vector<Instr> code;
  std::vector<uint32_t> source_lines;
};

struct Module {
  std::string source_file;
  std::vector<std::string> global_slots;
  std::vector<Function> functions;
  uint32_t entry = 0;
};

std::string dump_module(const Module& module);

} // namespace xlang3::ir
