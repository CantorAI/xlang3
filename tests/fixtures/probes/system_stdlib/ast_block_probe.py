# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import ast

print("ast-import", flush=True)
const_node = ast.Constant(9)
print("constant", flush=True)
bin_node = ast.BinOp(left=const_node, op=ast.Add(), right=ast.Constant(value=4))
print("binop", flush=True)
module_node = ast.Module(body=[ast.Expr(value=bin_node)], type_ignores=[])
print("module", flush=True)
print(ast.literal_eval(const_node), list(ast.iter_fields(bin_node))[0][0], len(list(ast.walk(module_node))), flush=True)
print("walk", flush=True)
print(ast.dump(bin_node), flush=True)
print("dump", flush=True)
print(isinstance(ast.parse("x = 1"), ast.Module), ast.parse("1", mode="eval").__class__.__name__, flush=True)
print("parse", flush=True)

class NameCollector(ast.NodeVisitor):
    def __init__(self):
        self.names = []

    def visit_Name(self, node):
        self.names.append(node.id)

collector = NameCollector()
collector.visit(ast.Module(body=[ast.Expr(value=ast.Name(id="seen", ctx=ast.Load()))], type_ignores=[]))
print(collector.names, flush=True)
