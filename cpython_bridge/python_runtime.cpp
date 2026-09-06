#include "engine.h"
#include "python_runtime.h"
#include "python_config.h"
#include <cstdlib>
#ifndef _WIN32
#include <dlfcn.h>
#endif

namespace x3py {
void ensure_python_runtime() {
  static std::mutex initialization;
  std::lock_guard<std::mutex> lock(initialization);
  if (Py_IsInitialized()) return;
#ifndef _WIN32
  // CPython extension modules resolve Python symbols through the global scope.
  Dl_info info{};
  if (!dladdr(reinterpret_cast<void*>(Py_Initialize), &info) ||
      !dlopen(info.dli_fname, RTLD_NOW | RTLD_GLOBAL))
    throw std::runtime_error("cannot expose the CPython shared library to extension modules");
#endif
  PyConfig config;
  PyConfig_InitPythonConfig(&config);
  config.install_signal_handlers = 0;
  config.parse_argv = 0;
  const char* executable = std::getenv("XLANG3_PYTHON_EXECUTABLE");
  if (!executable || !*executable) executable = XLANG3_PYTHON_EXECUTABLE;
  PyStatus status = PyConfig_SetBytesString(&config, &config.program_name, executable);
  if (!PyStatus_Exception(status))
    status = PyConfig_SetBytesString(&config, &config.executable, executable);
  const char* home = std::getenv("XLANG3_PYTHON_HOME");
  if (!home || !*home) home = XLANG3_PYTHON_HOME;
  if (!PyStatus_Exception(status)) status = PyConfig_SetBytesString(&config, &config.home, home);
  if (!PyStatus_Exception(status)) status = Py_InitializeFromConfig(&config);
  std::string error = PyStatus_Exception(status)
      ? (status.err_msg ? status.err_msg : "CPython initialization failed") : "";
  PyConfig_Clear(&config);
  if (!error.empty()) throw std::runtime_error(error);
  // CPython is process-owned, not finalized on a package or runtime unload.
  // Other runtimes and third-party extensions may still retain Python state.
  PyEval_SaveThread();
}
}
