#pragma once
#include "xlang3/xlang3.h"
#include <atomic>
#include <chrono>
#include <future>
#include <thread>
#include <stdexcept>

inline void TensorReadiness(X::Runtime& runtime) {
  auto require = [](bool ok, const char* message) { if (!ok) throw std::runtime_error(message); };
  auto tensor = X::Tensor::Create(runtime.host(), X3_TENSOR_FLOAT32, {2});
  auto alias = tensor.View({1}, {4}, 4);
  auto* data = static_cast<float*>(tensor.Info().data);
  auto producer = tensor.Acquire(X3_TENSOR_WRITE);
  std::thread thread([use = std::move(producer), data, &runtime]() mutable {
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    auto runtimeEntry = runtime.Dict();
    data[0] = 11; data[1] = 21;
    use.Finish();
  });
  // Graph CPU reads must release the runtime lock while waiting for producer.
  auto graph = X::TensorGraph(alias + alias);
  X::Tensor output(graph.Run());
  thread.join();
  require(*static_cast<float*>(output.Info().data) == 42, "delayed producer to CPU graph/view");

  // A batch with distinct tensor IDs on one storage must acquire one WRITE.
  {
    auto use = X::Tensor::AcquireMany({{tensor, X3_TENSOR_READ}, {alias, X3_TENSOR_WRITE}});
    data[1] = 23;
  }
  require(data[1] == 23, "batch promotes aliased write");
  auto other = X::Tensor::Create(runtime.host(), X3_TENSOR_FLOAT32, {1});
  std::atomic<int> completed{0};
  auto worker = [&](bool reverse) {
    for (int i = 0; i != 50; ++i) {
      auto use = reverse
          ? X::Tensor::AcquireMany({{other, X3_TENSOR_WRITE}, {alias, X3_TENSOR_WRITE}})
          : X::Tensor::AcquireMany({{tensor, X3_TENSOR_WRITE}, {other, X3_TENSOR_WRITE}});
      ++completed;
    }
  };
  std::thread first(worker, false), second(worker, true);
  first.join(); second.join();
  require(completed == 100, "opposite order batches complete");

  struct Async {
    std::shared_future<void> ready;
    std::atomic<int>* cleanup;
  };
  std::atomic<int> cleanups{0};
  std::promise<void> ready;
  auto* pending = new Async{ready.get_future().share(), &cleanups};
  X3TensorCompletion completion{}; completion.size = sizeof(completion);
  completion.context = pending;
  completion.wait = [](void* pointer, const X3TensorExecution*) -> X3Status {
    static_cast<Async*>(pointer)->ready.wait(); return X3_STATUS_OK;
  };
  completion.query = [](void* pointer) -> int32_t {
    return static_cast<Async*>(pointer)->ready.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
  };
  completion.cleanup = [](void* pointer) {
    auto* state = static_cast<Async*>(pointer);
    state->ready.wait(); ++*state->cleanup; delete state;
  };
  auto write = tensor.Acquire(X3_TENSOR_WRITE);
  write.Finish(&completion);
  std::thread delayed([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(30)); data[1] = 99; ready.set_value();
  });
  {
    auto read = alias.Acquire();
    require(*static_cast<float*>(alias.Info().data) == 99, "completion callback readiness across aliases");
  }
  delayed.join();
  // Trigger completed-fence retirement without dropping a still-live view.
  { auto read = alias.Acquire(); }
  require(cleanups == 1, "completion cleanup once after readiness");

  struct Modes { bool ready = false; int waits = 0; int cleanups = 0; } modes;
  auto modeCompletion = completion;
  modeCompletion.context = &modes;
  modeCompletion.wait = [](void* pointer, const X3TensorExecution*) -> X3Status {
    ++static_cast<Modes*>(pointer)->waits; return X3_STATUS_OK;
  };
  modeCompletion.query = [](void* pointer) -> int32_t { return static_cast<Modes*>(pointer)->ready; };
  modeCompletion.cleanup = [](void* pointer) { ++static_cast<Modes*>(pointer)->cleanups; };
  {
    auto batch = X::Tensor::AcquireMany({{tensor, X3_TENSOR_READ}, {other, X3_TENSOR_WRITE}});
    batch.Finish(&modeCompletion);
  }
  { auto read = alias.Acquire(); }
  require(modes.waits == 0, "batch output writes must not serialize independent weight readers");
  modes.ready = true;
  { auto read = alias.Acquire(); }
  require(modes.cleanups == 0, "batch callback retained by other storage");
  { auto read = other.Acquire(); }
  require(modes.cleanups == 1, "batch completion cleaned once across all storages");
  modes = Modes{};
  modeCompletion.wait = [](void*, const X3TensorExecution*) -> X3Status { return X3_STATUS_ERROR; };
  {
    auto write = tensor.Acquire(X3_TENSOR_WRITE);
    write.Finish(&modeCompletion);
  }
  bool waitFailed = false;
  try { auto read = alias.Acquire(); } catch (const std::exception&) { waitFailed = true; }
  require(waitFailed, "backend wait failure propagates through public ABI");
  modes.ready = true;
  { auto write = alias.Acquire(X3_TENSOR_WRITE); }
  require(modes.cleanups == 1, "failed acquisition releases its reservation");

  std::atomic<int> storageCleanup{0};
  float scalar = 0; int64_t shape = 1;
  X3TensorInfo info{}; info.size = sizeof(info); info.dtype = X3_TENSOR_FLOAT32;
  info.rank = 1; info.shape = &shape; info.data = &scalar; info.byte_size = sizeof(scalar);
  auto owned = X::Tensor::Wrap(runtime.host(), info, &storageCleanup,
      [](void* pointer) { ++*static_cast<std::atomic<int>*>(pointer); });
  auto view = owned.View({1}, {4});
  auto use = view.Acquire();
  owned = X::Tensor(); view = X::Tensor();
  require(storageCleanup == 0, "use retains storage after final view release");
  use.Finish();
  require(storageCleanup == 1, "storage cleanup after final use");

  info.readonly = 1;
  auto readonly = X::Tensor::Wrap(runtime.host(), info);
  X3TensorUse* invalid = nullptr;
  require(x3_tensor_begin_use(runtime.host()->runtime, readonly.raw(), X3_TENSOR_WRITE,
      nullptr, &invalid) == X3_STATUS_ERROR && !invalid, "readonly write lease rejected");
}
