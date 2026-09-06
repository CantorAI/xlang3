#pragma once
#include "engine.h"
namespace x3py {
X3Value python_buffer(const std::shared_ptr<Engine>& engine, PyObject* object);
PyObject* buffer_method(PyObject* module, PyObject* object);
int get_buffer(PyObject* object, Py_buffer* view, int flags);
void release_buffer(PyObject* object, Py_buffer* view);
}
