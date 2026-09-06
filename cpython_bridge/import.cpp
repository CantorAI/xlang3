#include "import.h"
#include "xlang_object.h"
#include <filesystem>

namespace x3py {
PyObject* import_module(PyObject* module, PyObject* args, PyObject* kwargs) {
  return protect([&]() -> PyObject* {
    const char* name = nullptr;
    const char* from = nullptr;
    const char* thru = nullptr;
    static const char* names[] = {"moduleName", "fromPath", "thru", nullptr};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "s|zz:importModule",
        const_cast<char**>(names), &name, &from, &thru)) return nullptr;
    if (!*name || (thru && *thru && from && *from)) {
      PyErr_SetString(PyExc_ValueError, "provide a module name and either fromPath or thru");
      return nullptr;
    }
    auto* state = static_cast<ModuleState*>(PyModule_GetState(module));
    if (!state || !state->engine || !state->proxy_type) {
      PyErr_SetString(PyExc_RuntimeError, "XLang3 bridge is closed");
      return nullptr;
    }
    auto engine = *state->engine;
    X3Value result = x3_value_invalid();
    engine->execute([&] {
      if (thru && *thru) {
        engine->check(x3_runtime_import_remote(engine->runtime, name, thru, &result));
      } else {
        const char* package = from;
        if (from && *from && std::filesystem::is_directory(std::filesystem::u8path(from))) {
          engine->check(x3_runtime_add_import_root(engine->runtime, from));
          package = nullptr;
        }
        engine->check(x3_runtime_import_module(engine->runtime, package, name, &result));
      }
    });
    return to_python(engine, reinterpret_cast<PyTypeObject*>(state->proxy_type), result);
  });
}
}
