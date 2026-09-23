/* Copyright (C) 2026 CantorAI Inc. and The XLang Foundation

   Licensed under the Apache License, Version 2.0. */
#pragma once

#include "xlang3/value.h"

namespace xlang3 {

class Runtime;
namespace ir { struct Function; }

// Explicit eval locals are consulted by the entry code on every name load.
// Nested functions use their normal globals, as in Python.
void push_eval_locals(Runtime& runtime, const ir::Function* function, const Value& locals);
void pop_eval_locals(Runtime& runtime);
const Value* current_eval_locals(Runtime& runtime, const ir::Function* function);

} // namespace xlang3
