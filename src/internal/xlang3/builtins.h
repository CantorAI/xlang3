#pragma once

#include "xlang3/runtime.h"

namespace xlang3 {

void register_core_builtins(Runtime& runtime);
void register_io_builtins(Runtime& runtime);

} // namespace xlang3
