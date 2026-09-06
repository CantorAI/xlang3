#include "engine.h"
#include "python_object.h"
#include "python_runtime.h"
#include "xlang_object.h"
#include "buffer.h"
#include "snapshot.h"
#include <string>

namespace x3py {
namespace {
struct NativeBridge {
  std::shared_ptr<Engine> engine;
  PyObject* owner = nullptr;
  PyObject* type = nullptr;
};

void cleanup(void* data) {
  auto* bridge = static_cast<NativeBridge*>(data);
  auto engine = bridge->engine;
  {
    EnterPython python(engine.get());
    close_borrowed_engine(bridge->engine);
    Py_XDECREF(bridge->owner);
    bridge->engine->owner = nullptr;
    Py_XDECREF(bridge->type);
    bridge->engine.reset();
  }
  delete bridge;
}

X3Status import_python(X3CallContext* context, X3Runtime*, void* data,
    const X3Value* args, uint32_t argc, X3Value* result) {
  auto* bridge = static_cast<NativeBridge*>(data);
  auto engine = bridge->engine;
  EnterPython python(engine.get());
  try {
    engine->ensure_open();
    const char* text = nullptr;
    uint64_t size = 0;
    engine->check(x3_value_string_data(engine->runtime, args[0], &text, &size));
    std::string name(text, static_cast<size_t>(size));
    if (name.empty() || name.find('\0') != std::string::npos)
      throw std::invalid_argument("CPython module name must be nonempty and contain no NUL");
    if (argc == 2) {
      engine->check(x3_value_string_data(engine->runtime, args[1], &text, &size));
      if (std::string_view(text, static_cast<size_t>(size)).find('\0') != std::string_view::npos)
        throw std::invalid_argument("CPython search path cannot contain NUL");
      PyObject* path = PyUnicode_DecodeUTF8(text, static_cast<Py_ssize_t>(size), "strict");
      if (!path) throw PythonError{};
      auto* paths = PySys_GetObject("path");
      int contains = paths ? PySequence_Contains(paths, path) : -1;
      int status = contains == 0 ? PyList_Insert(paths, 0, path) : contains < 0 ? -1 : 0;
      Py_DECREF(path);
      if (status < 0) throw PythonError{};
    }
    PyObject* module = PyImport_ImportModule(name.c_str());
    if (!module) throw PythonError{};
    try { *result = wrap_python(engine, module); }
    catch (...) { Py_DECREF(module); throw; }
    Py_DECREF(module);
    return X3_STATUS_OK;
  } catch (...) {
    translate_exception();
    PyObject* exception = PyErr_GetRaisedException();
    PyObject* message = exception ? PyObject_Str(exception) : nullptr;
    const char* text = message ? PyUnicode_AsUTF8(message) : nullptr;
    engine->python_host->raise_class_error(context, "ImportError", text ? text : "CPython import failed");
    Py_XDECREF(message);
    Py_XDECREF(exception);
    PyErr_Clear();
    return X3_STATUS_ERROR;
  }
}

enum class Action { Buffer, Dumps, Loads };
template<Action action>
X3Status operate(X3CallContext* context, X3Runtime*, void* data,
    const X3Value* args, uint32_t argc, X3Value* result) {
  auto engine = static_cast<NativeBridge*>(data)->engine;
  EnterPython python(engine.get());
  auto* output = protect([&]() -> PyObject* {
    engine->ensure_open();
    engine->run([&] { x3_value_retain(args[0]); });
    PyObject* object = to_python(engine, engine->proxy_type, args[0]);
    if (!object) throw PythonError{};
    struct Ref { PyObject* object; ~Ref() { Py_DECREF(object); } } owner{object};
    if constexpr (action == Action::Buffer) {
      *result = python_buffer(engine, object);
      return Py_NewRef(Py_None);
    } else {
      PyObject* converted;
      if constexpr (action == Action::Dumps) converted = dump_snapshot(engine, object);
      else converted = load_snapshot(engine, object, argc == 2 && args[1].tag == X3_TAG_BOOL && args[1].as.b);
      if (!converted) throw PythonError{};
      Ref answer{converted};
      *result = from_python(engine, engine->proxy_type, converted);
      return Py_NewRef(Py_None);
    }
  });
  if (output) { Py_DECREF(output); return X3_STATUS_OK; }
  PyObject* exception = PyErr_GetRaisedException();
  PyObject* message = exception ? PyObject_Str(exception) : nullptr;
  const char* text = message ? PyUnicode_AsUTF8(message) : nullptr;
  engine->python_host->raise_class_error(context, "ValueError", text ? text : "CPython bridge operation failed");
  Py_XDECREF(message);
  Py_XDECREF(exception);
  PyErr_Clear();
  return X3_STATUS_ERROR;
}
}
}

extern "C" XLANG3_PACKAGE_EXPORT const uint32_t xlang3_package_abi_version = X3_ABI_VERSION;

extern "C" XLANG3_PACKAGE_EXPORT X3Status Load(void* pointer, X3Value) {
  auto* host = static_cast<X3PackageHost*>(pointer);
  if (!host || host->abi_version != X3_ABI_VERSION) return X3_STATUS_ERROR;
  try {
    x3py::ensure_python_runtime();
    auto bridge = std::make_unique<x3py::NativeBridge>();
    bridge->engine = std::make_shared<x3py::Engine>(host->runtime);
    x3py::EnterPython python(bridge->engine.get());
    bridge->type = x3py::create_proxy_type(nullptr);
    if (!bridge->type) throw x3py::PythonError{};
    bridge->engine->proxy_type = reinterpret_cast<PyTypeObject*>(bridge->type);
    bridge->owner = x3py::create_engine_owner(bridge->engine);
    if (!bridge->owner) { Py_DECREF(bridge->type); throw x3py::PythonError{}; }
    bridge->engine->owner = bridge->owner;
    auto* registered = bridge.release();
    if (host->package_set_cleanup(host, registered, x3py::cleanup) != X3_STATUS_OK) {
      x3py::cleanup(registered);
      return X3_STATUS_ERROR;
    }
    x3py::initialize_python_class(registered->engine, "_xlang3_hosted_cpython_bridge");
    X3Module* module = nullptr;
    if (host->add_module(host, "cpython", &module) != X3_STATUS_OK) return X3_STATUS_ERROR;
    X3NativeFunctionDef function{};
    function.size = sizeof(function);
    function.name = "importModule";
    function.callback = x3py::import_python;
    function.user_data = registered;
    function.min_argc = 1;
    function.max_argc = 2;
    if (host->module_add_function(module, &function) != X3_STATUS_OK) return X3_STATUS_ERROR;
    struct Operation { const char* name; X3NativeFn function; uint32_t max; };
    const Operation operations[] = {
      {"buffer", x3py::operate<x3py::Action::Buffer>, 1},
      {"dumps", x3py::operate<x3py::Action::Dumps>, 1},
      {"loads", x3py::operate<x3py::Action::Loads>, 2}
    };
    for (const auto& operation : operations) {
      function.name = operation.name;
      function.callback = operation.function;
      function.max_argc = operation.max;
      if (host->module_add_function(module, &function) != X3_STATUS_OK) return X3_STATUS_ERROR;
    }
    return X3_STATUS_OK;
  } catch (...) {
    return X3_STATUS_ERROR;
  }
}
