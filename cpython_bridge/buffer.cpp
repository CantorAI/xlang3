#include "buffer.h"
#include "xlang_object.h"

namespace x3py {
namespace {
struct PythonBuffer {
  Py_buffer view{};
  std::weak_ptr<Engine> engine;
};
void destroy_python_buffer(void* data) {
  auto* buffer = static_cast<PythonBuffer*>(data);
  auto gil = PyGILState_Ensure();
  if (auto engine = buffer->engine.lock()) engine->buffers.erase(buffer);
  PyBuffer_Release(&buffer->view);
  delete buffer;
  PyGILState_Release(gil);
}
struct Export {
  X3Buffer* handle = nullptr;
  X3BufferInfo info{};
  Py_ssize_t shape = 0;
  Py_ssize_t stride = 0;
};
}

X3Value python_buffer(const std::shared_ptr<Engine>& engine, PyObject* object) {
  engine->ensure_open();
  auto buffer = std::make_unique<PythonBuffer>();
  if (PyObject_GetBuffer(object, &buffer->view, PyBUF_CONTIG_RO) < 0) throw PythonError{};
  buffer->engine = engine;
  try { engine->buffers.emplace(buffer.get(), buffer->view.obj); }
  catch (...) { PyBuffer_Release(&buffer->view); throw; }
  auto* owned = buffer.release();
  auto result = engine->run([&] {
    return x3_value_memoryview(engine->runtime, owned->view.buf,
        static_cast<uint64_t>(owned->view.len), owned->view.readonly,
        owned, destroy_python_buffer);
  });
  if (result.tag == X3_TAG_INVALID) throw std::runtime_error("cannot create shared buffer view");
  return result;
}

PyObject* buffer_method(PyObject* module, PyObject* object) {
  return protect([&]() -> PyObject* {
    auto* state = static_cast<ModuleState*>(PyModule_GetState(module));
    auto engine = *state->engine;
    return to_python(engine, engine->proxy_type, python_buffer(engine, object));
  });
}

int get_buffer(PyObject* object, Py_buffer* view, int flags) {
  try {
    auto* self = reinterpret_cast<XlangObject*>(object);
    auto engine = *self->engine;
    auto buffer = std::make_unique<Export>();
    engine->execute([&] {
      engine->check(x3_buffer_acquire(engine->runtime, self->value,
          (flags & PyBUF_WRITABLE) != 0, &buffer->handle, &buffer->info));
    });
    if (buffer->info.size > PY_SSIZE_T_MAX) {
      engine->run([&] { x3_buffer_release(buffer->handle); });
      throw std::overflow_error("buffer exceeds CPython size limits");
    }
    if (PyBuffer_FillInfo(view, object, buffer->info.data,
        static_cast<Py_ssize_t>(buffer->info.size), buffer->info.readonly, flags) < 0) {
      engine->run([&] { x3_buffer_release(buffer->handle); });
      return -1;
    }
    view->itemsize = static_cast<Py_ssize_t>(buffer->info.item_size);
    buffer->shape = view->len / view->itemsize;
    buffer->stride = view->itemsize;
    view->format = (flags & PyBUF_FORMAT) ? const_cast<char*>(buffer->info.format) : nullptr;
    view->shape = (flags & PyBUF_ND) ? &buffer->shape : nullptr;
    view->strides = (flags & PyBUF_STRIDES) == PyBUF_STRIDES ? &buffer->stride : nullptr;
    view->internal = buffer.release();
    return 0;
  } catch (...) {
    translate_exception();
    return -1;
  }
}

void release_buffer(PyObject* object, Py_buffer* view) {
  auto* buffer = static_cast<Export*>(view->internal);
  if (!buffer) return;
  auto* self = reinterpret_cast<XlangObject*>(object);
  (*self->engine)->run([&] { x3_buffer_release(buffer->handle); });
  delete buffer;
  view->internal = nullptr;
}
}
