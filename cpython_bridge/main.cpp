#include "engine.h"
#include "import.h"
#include "xlang_object.h"
#include "python_object.h"
#include "buffer.h"
#include "snapshot.h"
#include <string>
#include <vector>

namespace x3py {
namespace {
int exec_module(PyObject* module) {
  auto* result = protect([&]() -> PyObject* {
    auto* state = static_cast<ModuleState*>(PyModule_GetState(module));
    if (state->engine) return Py_NewRef(Py_None);
    state->engine = new std::shared_ptr<Engine>(std::make_shared<Engine>());
    state->proxy_type = create_proxy_type(module);
    if (!state->proxy_type) return nullptr;
    if (PyModule_AddObjectRef(module, "Object", state->proxy_type) < 0) return nullptr;
    auto engine = *state->engine;
    engine->proxy_type = reinterpret_cast<PyTypeObject*>(state->proxy_type);
    state->owner = create_engine_owner(engine);
    if (!state->owner) return nullptr;
    engine->owner = state->owner;
    initialize_python_class(engine);
    // Match normal Python script search locations without taking over imports.
    PyObject* paths = PySys_GetObject("path");
    std::vector<std::string> roots;
    if (paths && PyList_Check(paths)) {
      for (Py_ssize_t i = 0; i < PyList_GET_SIZE(paths); ++i) {
        PyObject* path = PyList_GET_ITEM(paths, i);
        if (!PyUnicode_Check(path)) continue;
        const char* text = PyUnicode_AsUTF8(path);
        if (!text) throw PythonError{};
        roots.emplace_back(*text ? text : ".");
      }
    }
    engine->run([&] {
      for (const auto& root : roots)
        engine->check(x3_runtime_add_import_root(engine->runtime, root.c_str()));
    });
    return Py_NewRef(Py_None);
  });
  if (!result) return -1;
  Py_DECREF(result);
  return 0;
}
int traverse(PyObject* module, visitproc visit, void* arg) {
  auto* state = static_cast<ModuleState*>(PyModule_GetState(module));
  Py_VISIT(state->proxy_type);
  Py_VISIT(state->owner);
  return 0;
}
int clear(PyObject* module) {
  auto* state = static_cast<ModuleState*>(PyModule_GetState(module));
  Py_CLEAR(state->proxy_type);
  Py_CLEAR(state->owner);
  return 0;
}
void free_module(void* module) {
  auto* state = static_cast<ModuleState*>(PyModule_GetState(static_cast<PyObject*>(module)));
  delete state->engine;
  state->engine = nullptr;
}
PyMethodDef methods[] = {
  {"buffer", buffer_method, METH_O, "Create a shared contiguous byte view."},
  {"dumps", dumps_method, METH_O, "Serialize an object graph."},
  {"loads", reinterpret_cast<PyCFunction>(loads_method), METH_VARARGS | METH_KEYWORDS,
   "Restore a trusted object snapshot; requires trusted=True."},
  {"importModule", reinterpret_cast<PyCFunction>(import_module), METH_VARARGS | METH_KEYWORDS,
   "importModule(moduleName, fromPath=None, thru=None) -> XLang3 object"},
  {nullptr, nullptr, 0, nullptr}
};
PyModuleDef_Slot slots[] = {
  {Py_mod_exec, reinterpret_cast<void*>(exec_module)},
  {Py_mod_multiple_interpreters, Py_MOD_MULTIPLE_INTERPRETERS_NOT_SUPPORTED},
  {Py_mod_gil, Py_MOD_GIL_USED},
  {0, nullptr}
};
PyModuleDef definition = {
  PyModuleDef_HEAD_INIT, "xlang3", "XLang3 runtime bridge", sizeof(ModuleState),
  methods, slots, traverse, clear, free_module
};
}
}
PyMODINIT_FUNC PyInit_xlang3() { return PyModuleDef_Init(&x3py::definition); }
