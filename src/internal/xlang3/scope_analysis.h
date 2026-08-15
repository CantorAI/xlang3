#pragma once

#include "xlang3/ast.h"

#include <string>
#include <unordered_set>
#include <vector>

namespace xlang3::sema {

using NameSet = std::unordered_set<std::string>;

bool contains(const NameSet& names, const std::string& name);
std::vector<std::string> local_names_for(const std::vector<std::string>& params, const std::vector<ast::StmtPtr>& body);
std::vector<std::string> free_candidates_for(const ast::FunctionDef& fn);

} // namespace xlang3::sema
