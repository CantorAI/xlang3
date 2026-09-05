#include "xlang3/abi/xapi.h"
#include "xlang3/c_api_bridge.h"
#include "xlang3/runtime.h"
#include "ipc/ipc_runtime.h"
#include "runtime_lock.h"
#include <stdexcept>

X3Status x3_runtime_import_remote(X3Runtime* runtime, const char* name,
    const char* endpoint, X3Value* result) {
  auto* rt = reinterpret_cast<xlang3::Runtime*>(runtime);
  if (!rt) return X3_STATUS_ERROR;
  try {
    if (!name || !*name || !endpoint || !*endpoint || !result)
      throw std::runtime_error("remote import requires a name, endpoint, and result");
    xlang3::XlangRuntimeExecutionGuard guard;
    xlang3::Value object;
    std::string error;
    if (!xlang3::ipc_import_thru(*rt, name, xlang3::Value::string(endpoint), object, error))
      throw std::runtime_error(error);
    *result = xlang3::to_c_value(object);
    return X3_STATUS_OK;
  } catch (const std::exception& error) {
    rt->set_last_error(error.what());
    return X3_STATUS_ERROR;
  }
}
