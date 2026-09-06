#include "engine.h"
#include "python_object.h"
#include <stdexcept>

namespace x3py {
Engine::Engine() {
  runtime = run([] { return x3_runtime_create(); });
  if (!runtime) throw std::runtime_error("cannot create XLang3 runtime");
}
unsigned& Engine::depth(Engine* engine) {
  static thread_local std::unordered_map<Engine*, unsigned> depths;
  return depths[engine];
}
Engine::~Engine() {
  closing = true;
  auto gil = PyGILState_Ensure();
  {
    AllowThreads allow;
    x3_value_release(python_class);
    if (owns_runtime) x3_runtime_destroy(runtime);
  }
  PyGILState_Release(gil);
}
void Engine::check(X3Status status) {
  if (status != X3_STATUS_OK) {
    const char* error = x3_runtime_last_error(runtime);
    throw std::runtime_error(error && *error ? error : "XLang3 operation failed");
  }
}
PyObject* translate_exception() {
  try { throw; }
  catch (const PythonError&) { if (!PyErr_Occurred()) PyErr_SetString(PyExc_RuntimeError, "Python operation failed"); }
  catch (const ProtocolError& e) {
    auto* type = PyDict_GetItemString(PyEval_GetBuiltins(), e.type.c_str());
    PyErr_SetString(type && PyExceptionClass_Check(type) ? type : PyExc_RuntimeError, e.what());
  }
  catch (const std::bad_alloc&) { PyErr_NoMemory(); }
  catch (const std::exception& e) { PyErr_SetString(PyExc_RuntimeError, e.what()); }
  catch (...) { PyErr_SetString(PyExc_RuntimeError, "unexpected XLang3 bridge error"); }
  return nullptr;
}

void Engine::check_protocol(X3Status status, const char* fallback) {
  if (status == X3_STATUS_OK) return;
  std::string message = x3_runtime_last_error(runtime);
  const char* type = x3_runtime_take_exception_type(runtime);
  throw ProtocolError(type ? type : fallback, std::move(message));
}
}
