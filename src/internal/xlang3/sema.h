#pragma once

#include "xlang3/ast.h"
#include "xlang3/ir.h"

#include <string>
#include <vector>

namespace xlang3 {

struct LowerResult {
  ir::Module module;
  std::vector<std::string> errors;
};

LowerResult lower_to_ir(const ast::Module& module);

} // namespace xlang3
