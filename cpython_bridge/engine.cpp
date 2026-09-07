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
unsigned& Engine::call_depth(const Engine* engine) {
  static thread_local std::unordered_map<const Engine*, unsigned> depths;
  return depths[engine];
}
Engine::~Engine() {
  closing = true;
  if (!runtime) return;
  auto gil = PyGILState_Ensure();
  {
    AllowThreads allow;
    x3_value_release(python_class);
    if (owns_runtime) x3_runtime_destroy(runtime);
  }
  PyGILState_Release(gil);
}
void Engine::close() {
  {
    std::lock_guard<std::mutex> lock(activity_mutex);
    if (closing) return;
    closing = true;
  }
  {
    AllowThreads allow;
    std::unique_lock<std::mutex> lock(activity_mutex);
    activity_drained.wait(lock, [&] { return active_calls == 0; });
  }
  auto owner = shared_from_this();
  close_borrowed_engine(owner);
  if (owns_runtime && runtime) {
    AllowThreads allow;
    x3_runtime_destroy(runtime);
    runtime = nullptr;
    python_host = nullptr;
  }
}

namespace {
constexpr const char* shutdown_capsule_name = "xlang3.interpreter_shutdown";
void delete_shutdown_reference(PyObject* capsule) {
  delete static_cast<std::weak_ptr<Engine>*>(PyCapsule_GetPointer(capsule, shutdown_capsule_name));
}
PyObject* shutdown_interpreter(PyObject* capsule, PyObject*) {
  return protect([&]() -> PyObject* {
    auto* reference = static_cast<std::weak_ptr<Engine>*>(PyCapsule_GetPointer(capsule, shutdown_capsule_name));
    if (!reference) throw PythonError{};
    if (auto engine = reference->lock()) engine->close();
    Py_RETURN_NONE;
  });
}
PyMethodDef shutdown_method = {"_shutdown_runtime", shutdown_interpreter, METH_NOARGS, nullptr};
}
void register_interpreter_shutdown(const std::shared_ptr<Engine>& engine) {
  auto reference = std::make_unique<std::weak_ptr<Engine>>(engine);
  PyObject* capsule = PyCapsule_New(reference.get(), shutdown_capsule_name, delete_shutdown_reference);
  if (!capsule) throw PythonError{};
  reference.release();
  PyObject* callback = PyCFunction_New(&shutdown_method, capsule);
  Py_DECREF(capsule);
  if (!callback) throw PythonError{};
  PyObject* atexit = PyImport_ImportModule("atexit");
  PyObject* registered = atexit ? PyObject_CallMethod(atexit, "register", "O", callback) : nullptr;
  Py_XDECREF(atexit);
  Py_DECREF(callback);
  if (!registered) throw PythonError{};
  Py_DECREF(registered);
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
