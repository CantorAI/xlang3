#pragma once
#include "engine.h"
namespace x3py {
PyObject* dump_snapshot(const std::shared_ptr<Engine>&, PyObject*);
PyObject* load_snapshot(const std::shared_ptr<Engine>&, PyObject*, bool trusted);
PyObject* dumps_method(PyObject*, PyObject*);
PyObject* loads_method(PyObject*, PyObject*, PyObject*);
}
