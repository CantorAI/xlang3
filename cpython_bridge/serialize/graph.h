#pragma once
#include "../engine.h"

namespace x3py {
void write_python_graph(Engine& engine, X3Stream* stream, PyObject* root);
PyObject* read_python_graph(Engine& engine, X3Stream* stream);
}
