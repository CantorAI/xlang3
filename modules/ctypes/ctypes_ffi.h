/* Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
   Licensed under the Apache License, Version 2.0. */
#pragma once

#include "xlang3/runtime.h"

namespace xlang3 {

bool ctypes_foreign_load_library(Runtime& runtime, const Value* args,
                                 uint32_t argc, Value& out,
                                 std::string& error);
bool ctypes_foreign_call(Runtime& runtime, const Value* args,
                         uint32_t argc, Value& out,
                         std::string& error);
bool ctypes_type_layout(const Value& type, size_t& size, size_t& alignment);
bool ctypes_pointer_read(Runtime& runtime, const Value& pointer, int64_t index,
                         Value& out, std::string& error);

} // namespace xlang3
