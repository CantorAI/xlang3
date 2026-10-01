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
#include <memory>
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
  std::vector<uint32_t> star_args;
  std::vector<uint32_t> kw_star_args;
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
  ImportModuleThru,
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
  MatMul,
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
  CallMethodEx,
  CallEx,
  Call,
  Await,
  Yield,
  YieldFrom,
  Pop,
  Return,
  CaptureExpressions,
  SetException,
  InplaceAdd,
  InplaceAddLocalConst,
  LoadLocalInstanceSlot,
  StoreLocalInstanceSlot,
  LoadLocalPair,
  LoadLocalConst,
  LoadConstPair,
  StoreLocalLoadLocal,
  JumpIfFalseLoadLocal,
  LoadLocalAttr,
  CallLocal,
  CallLocalMethod,
  CompareJumpIfFalse,
  IsJumpIfFalse,
  MoveJumpIfFalse,
  StoreLocalPair,
  MoveJumpIfTrue,
  ReturnConst,
  ReturnLocal,
  LoadLocalGlobal,
  LoadGlobalLocal,
  CallGlobal,
  DictSetConst,
  LoadInstanceSlotLocal,
  IterNextLocal,
  NotJumpIfFalse,
  LoadModuleAttr,
  LoadLocalGetItem,
  InplaceAddLocalLocal,
  JumpIfLocalLocalFalse,
  IsLocalConstJumpIfFalse,
  GuardedLocalNumericExpr,
  ForRangeConstLocalSum,
  ForLocalMoveAddLoop,
  ForCallAccumulateLoop,
  ForConstructMethodAccumulateLoop,
  ForScalarArithmeticLoop,
  ForPropertyAccessLoop,
  GetItemConst,
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

// Guarded local-add fusion stores its normal-IR fallback length in Instr::c.
// Legacy AddLocal* instructions keep c == 0 and retain their original meaning.
constexpr uint32_t kGuardedLocalAddFlag = 0x80000000u;
constexpr uint32_t kGuardedLocalAddSpanMask = ~kGuardedLocalAddFlag;
constexpr uint32_t kGuardedLocalMoveFlag = 0x80000000u;
constexpr uint32_t kGuardedLocalMoveSpanMask = ~kGuardedLocalMoveFlag;
// ForRangeConstLocalSum packs a guarded accumulator local beside its
// range-spec index. Large indices fall back to ordinary loop bytecode.
constexpr uint32_t kRangeSumFusionFlag = 0x80000000u;
constexpr uint32_t kRangeSumAccumulatorShift = 16;
constexpr uint32_t kRangeSumAccumulatorMask = 0x7fff0000u;
constexpr uint32_t kRangeSumSpecMask = 0x0000ffffu;
// ForLocalMoveAddLoop is only emitted for a contiguous move chain followed by
// a guarded local increment. Its c field packs the chain length and step const.
constexpr uint32_t kLocalMoveLoopMoveCountShift = 16;
constexpr uint32_t kLocalMoveLoopMoveCountMask = 0xffff0000u;
constexpr uint32_t kLocalMoveLoopStepConstantMask = 0x0000ffffu;
// The same guarded loop opcode can batch exact list.append(index) loops.
// c packs this flag, a 15-bit list local, and the 16-bit increment constant.
constexpr uint32_t kLocalAppendLoopFlag = 0x80000000u;
constexpr uint32_t kLocalAppendLoopListShift = 16;
constexpr uint32_t kLocalAppendLoopListMask = 0x7fff0000u;
// ForCallAccumulateLoop keeps the local receiving an inlined call's sum.
constexpr uint32_t kCallAccumulateLoopFlag = 0x80000000u;
constexpr uint32_t kCallAccumulateLoopLocalShift = 16;
constexpr uint32_t kCallAccumulateLoopLocalMask = 0x7fff0000u;
// ForConstructMethodAccumulateLoop packs the accumulator local in c as well.
constexpr uint32_t kConstructMethodAccumulateLoopFlag = 0x80000000u;
constexpr uint32_t kConstructMethodAccumulateLoopLocalShift = 16;
constexpr uint32_t kConstructMethodAccumulateLoopLocalMask = 0x7fff0000u;
// ForScalarArithmeticLoop consumes a guarded seven-node integer accumulator plan.
constexpr uint32_t kScalarArithmeticLoopFlag = 0x80000000u;
// ForPropertyAccessLoop packs its sum local alongside an exact-property-loop flag.
constexpr uint32_t kPropertyAccessLoopFlag = 0x80000000u;
constexpr uint32_t kPropertyAccessLoopLocalShift = 16;
constexpr uint32_t kPropertyAccessLoopLocalMask = 0x7fff0000u;
// Call may keep a simple add result in its sole local consumer and skip the
// following Add/StoreLocal pair. High bit marks the packed local slot.
constexpr uint32_t kCallAccumulateLocalFlag = 0x80000000u;
constexpr uint32_t kCallAccumulateLocalMask = 0x7fffffffu;

enum class GuardedLocalNumericExprNodeKind : uint8_t {
  Local,
  Constant,
  Add,
  Sub,
  Mul,
};

struct GuardedLocalNumericExprNode {
  GuardedLocalNumericExprNodeKind kind = GuardedLocalNumericExprNodeKind::Local;
  // Local and Constant nodes use `a`; arithmetic nodes reference earlier
  // nodes with `a` and `b`, making the expression plan a compact postorder DAG.
  uint32_t a = 0;
  uint32_t b = 0;
};

struct GuardedLocalNumericExprSpec {
  std::vector<GuardedLocalNumericExprNode> nodes;
  // Number of ordinary IR instructions immediately following the fused op.
  uint32_t fallback_span = 0;
};

constexpr uint32_t kMaxGuardedLocalNumericExprNodes = 16;

struct SourcePosition {
  uint32_t line = 0;
  uint32_t end_line = 0;
  uint32_t column = 0;
  uint32_t end_column = 0;
};

struct Function;

struct FunctionExecutionMetadata {
  const Function* owner = nullptr;
  std::vector<size_t> register_last_use;
  std::vector<bool> register_loop_carried;
  // Precomputed IR sites whose execution may populate an owning inline cache.
  std::vector<uint32_t> cache_cleanup_instructions;
};

struct Function {
  struct LogicalFrameRange {
    uint32_t start_instruction = 0;
    uint32_t end_instruction = 0;
    uint32_t function_id = 0;
    uint32_t locals_slot = UINT32_MAX;
  };

  std::string name;
  std::string qualname;
  std::string doc;
  std::vector<std::string> type_params;
  bool is_generator = false;
  bool is_async = false;
  bool is_coroutine = false;
  uint32_t first_line = 0;
  std::vector<std::string> params;
  std::vector<Param> signature;
  std::vector<std::string> locals;
  std::vector<uint32_t> cell_slots;
  std::vector<std::string> free_vars;
  uint32_t register_count = 0;
  std::vector<Value> constants;
  std::vector<std::string> names;
  std::vector<LogicalFrameRange> logical_frame_ranges;
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
  std::vector<GuardedLocalNumericExprSpec> guarded_local_numeric_exprs;
  std::vector<Instr> code;
  std::vector<uint32_t> source_lines;
  std::vector<SourcePosition> source_positions;
  // Derived immutable VM metadata is populated on first execution. It is not
  // serialized because older and embedded IR remain self-contained.
  mutable std::shared_ptr<const FunctionExecutionMetadata> execution_metadata;
};

struct Module {
  std::string source_file;
  std::vector<std::string> global_slots;
  std::vector<Function> functions;
  uint32_t entry = 0;
};

std::string dump_module(const Module& module);

} // namespace xlang3::ir
