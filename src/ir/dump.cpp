#include "xlang3/ir.h"

#include <sstream>

namespace xlang3::ir {

std::string dump_module(const Module& module) {
  std::ostringstream os;
  for (size_t fn_i = 0; fn_i < module.functions.size(); ++fn_i) {
    const auto& fn = module.functions[fn_i];
    os << "function #" << fn_i << " " << fn.name << "\n";
    os << "  locals:";
    for (size_t i = 0; i < fn.locals.size(); ++i) {
      os << " %" << i << "=" << fn.locals[i];
    }
    os << "\n";
    for (size_t ip = 0; ip < fn.code.size(); ++ip) {
      const auto& in = fn.code[ip];
      os << "  " << ip << ": op=" << static_cast<uint32_t>(in.op)
         << " dst=" << in.dst << " a=" << in.a << " b=" << in.b << " c=" << in.c << "\n";
    }
  }
  return os.str();
}

} // namespace xlang3::ir
