#pragma once
#include "common.h"

namespace x3py::graph {
bool has_custom_reduction(PyObject* object);
bool has_registered_reduction(PyObject* object);
Ref reduction_recipe(PyObject* object);
}
