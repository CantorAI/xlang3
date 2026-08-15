#include "xlang3/builtins.h"

namespace xlang3 {

void register_core_builtins(Runtime& runtime) {
  register_io_builtins(runtime);
}

} // namespace xlang3
