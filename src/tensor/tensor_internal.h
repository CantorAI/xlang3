#pragma once
#include "xlang3/abi/xtensor.h"
#include "xlang3/value.h"
#include "xlang3/runtime.h"
#include "xlang3/sequence.h"
#include "xlang3/mapping.h"
#include <atomic>
#include <memory>
#include <stdexcept>
#include <unordered_map>

namespace xlang3::tensor {
struct Storage {
  void* data = nullptr;
  uint64_t bytes = 0;
  int device = 0, device_id = 0;
  bool readonly = false;
  void* owner = nullptr;
  void (*cleanup)(void*) = nullptr;
  ~Storage() { if (cleanup) cleanup(owner); }
};
struct Registration {
  Runtime* runtime = nullptr;
  std::string provider, name;
  uint32_t arity = 0, flags = 0;
  X3TensorVisitor replay = nullptr;
  void* context = nullptr;
  void (*cleanup)(void*) = nullptr;
  ~Registration() { if (cleanup) cleanup(context); }
};
struct Operator {
  std::shared_ptr<Registration> registration;
  std::string name;
  Value attributes;
  Value left;
};
struct Tensor {
  uint64_t id = 0;
  Runtime* runtime = nullptr;
  X3TensorDType dtype = X3_TENSOR_FLOAT32;
  std::vector<int64_t> shape, strides;
  uint64_t offset = 0;
  std::shared_ptr<Storage> storage;
  std::string name;
  std::shared_ptr<Registration> registration;
  std::vector<Value> operands;
  Value attributes;
  Value regions;
};
enum class Kernel { Input, Constant, Add, Sub, Mul, Div, Neg, Relu, Exp, Matmul, Sum, Reshape, Permute, Unsupported };
struct Step { Value output; std::vector<Value> dependencies; Kernel kernel = Kernel::Unsupported; };
struct Graph { Runtime* runtime = nullptr; Value outputs; std::vector<Step> steps; };

constexpr const char* tensor_type = "xlang3.tensor";
constexpr const char* operator_type = "xlang3.tensor.operator";
constexpr const char* graph_type = "xlang3.tensor.graph";
Tensor* get(const Value&);
Operator* get_operator(const Value&);
Graph* get_graph(const Value&);
uint64_t item_size(X3TensorDType);
uint64_t count(const std::vector<int64_t>&);
std::vector<int64_t> contiguous_strides(const std::vector<int64_t>&, uint64_t);
void validate_layout(const Tensor&);
bool contiguous(const Tensor&);
std::unique_ptr<Tensor> allocate(Runtime&, X3TensorDType, std::vector<int64_t>);
Value wrap_tensor(Runtime&, std::unique_ptr<Tensor>);
Value wrap_operator(Runtime&, std::unique_ptr<Operator>);
Value wrap_graph(Runtime&, std::unique_ptr<Graph>);
Value type(Runtime&, const char*);
Value snapshot(const Value&);
void dependencies(const Value&, std::vector<Value>&);
Value substitute(const Value&, const std::unordered_map<uint64_t, Value>&);
Value apply(Runtime&, const Value&, const Value&, const std::string&);
bool handles(const Value&, const Value&);
bool binary(const Value&, const Value&, const char*, Value&, std::string&);
Value operation(Runtime&, const Operator&, std::vector<Value>);
Value build_graph(Runtime&, const Value&);
Value run(Runtime&, const Graph&, const Value& bindings);
void replay(Runtime&, const Graph&, X3TensorVisitor, void*);
Value inspect(const Graph&);
Kernel resolve(const Tensor&);
Value compute(Runtime&, const Tensor&, Kernel, const std::vector<Value>&, const Value&);
Value attr(const Value&, const std::string&, Value fallback = Value::invalid());
Value record(std::initializer_list<std::pair<const char*, Value>>);
std::vector<int64_t> dimensions(const Value&, bool validate_shape = true);
void register_module(Runtime&);
Value current_regions(Runtime&);
Value make_factory(Runtime&, std::shared_ptr<Registration>);
}
