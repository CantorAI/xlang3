#include "xlang3/ir.h"

#include <sstream>

namespace xlang3::ir {

namespace {

const char* op_name(Op op) {
  switch (op) {
    case Op::LoadConst: return "LoadConst";
    case Op::LoadLocal: return "LoadLocal";
    case Op::StoreLocal: return "StoreLocal";
    case Op::LoadGlobal: return "LoadGlobal";
    case Op::StoreGlobal: return "StoreGlobal";
    case Op::BuiltinPrint: return "BuiltinPrint";
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
