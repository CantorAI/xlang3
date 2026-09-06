#pragma once
#include "engine.h"

namespace x3py {
struct PythonPayload {
  Engine* engine;
  PyObject* object;
  X3Value instance; // Borrowed: removed from the identity table before destruction.
};
PyObject* create_engine_owner(const std::shared_ptr<Engine>& engine);
void close_borrowed_engine(const std::shared_ptr<Engine>& engine);
void initialize_python_class(const std::shared_ptr<Engine>& engine,
    const char* package_name = "_xlang3_cpython_bridge");
X3Value wrap_python(const std::shared_ptr<Engine>& engine, PyObject* object);
PyObject* unwrap_python(const std::shared_ptr<Engine>& engine, X3Value value);
}
