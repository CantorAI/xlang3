#pragma once

#include "xlang3/value.h"

#include <cstdint>
#include <string>
#include <vector>

namespace xlang3::ir {

enum class Op : uint16_t {
  LoadConst,
  LoadLocal,
  StoreLocal,
  LoadCell,
  StoreCell,
  LoadCellObject,
  LoadFree,
  StoreFree,
  LoadFreeObject,
  LoadGlobal,
  StoreGlobal,
  MakeFunction,
  MakeTuple,
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
  std::vector<std::vector<uint32_t>> call_args;
  std::vector<std::vector<uint32_t>> tuple_items;
  std::vector<std::vector<uint32_t>> function_closures;
  std::vector<Instr> code;
};

struct Module {
  std::vector<Function> functions;
  uint32_t entry = 0;
};

std::string dump_module(const Module& module);

} // namespace xlang3::ir
