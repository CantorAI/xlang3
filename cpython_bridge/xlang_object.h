#pragma once
#include "engine.h"

namespace x3py {
struct XlangObject {
  PyObject_HEAD
  std::shared_ptr<Engine>* engine;
  X3Value value;
  PyObject* owner;
};
PyObject* create_proxy_type(PyObject* module);
PyObject* to_python(const std::shared_ptr<Engine>& engine, PyTypeObject* type, X3Value value);
X3Value from_python(const std::shared_ptr<Engine>& engine, PyTypeObject* type, PyObject* object);
}
