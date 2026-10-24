/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0
*/
#pragma once

#include "xlang3/value.h"

namespace xlang3 {

// Resolve a stable key only for a bound, unmodified native ContextVar.get.
// The second helper reads its active explicit binding; missing values remain
// on the ordinary ContextVar.get path so defaults and errors stay unchanged.
Object* contextvar_exact_getter_key(const Value& bound_getter);
bool contextvar_lookup_if_set(Object* variable_key, Value& out);

} // namespace xlang3
