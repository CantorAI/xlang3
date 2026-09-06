#ifndef XLANG3_ABI_TENSOR_H
#define XLANG3_ABI_TENSOR_H
#include "xlang3/abi/xapi.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum X3TensorDType {
  X3_TENSOR_FLOAT32 = 1, X3_TENSOR_FLOAT64 = 2,
  X3_TENSOR_INT32 = 3, X3_TENSOR_INT64 = 4
} X3TensorDType;

/* Shape/strides are borrowed until the tensor is released. Strides are bytes.
   data is null for symbolic tensors, and device memory must not be dereferenced
   by CPU clients. Metadata is immutable after construction. rank is UINT32_MAX
   and dtype is 0 for expressions whose metadata has not been inferred.
   byte_size is the accessible storage span starting at data, including gaps
   in a strided view; it is not necessarily element_count * item_size. */
typedef struct X3TensorInfo {
  uint32_t size;
  X3TensorDType dtype;
  uint32_t rank;
  const int64_t* shape;
  const int64_t* strides;
  void* data;
  uint64_t byte_size;
  uint64_t id;
  int32_t device_type; /* 0 = CPU; other values belong to external backends. */
  int32_t device_id;
  int32_t readonly;
  int32_t symbolic;
} X3TensorInfo;

/* Values and strings supplied to replay are borrowed for the callback only.
   inputs includes tensor dependencies in attributes, in addition to operands.
   attributes retains the original names/container structure of those values. */
typedef struct X3TensorOperation {
  uint32_t size;
  uint64_t id;
  const char* provider;
  const char* name;
  const X3Value* inputs;
  uint32_t input_count;
  uint32_t operand_count;
  X3Value attributes;
  X3Value output;
  X3Value regions;
  uint32_t flags;
} X3TensorOperation;
typedef X3Status (*X3TensorVisitor)(X3Runtime*, void*, const X3TensorOperation*);
#define X3_TENSOR_ORDERED 1u
typedef struct X3TensorOperatorDef {
  uint32_t size;
  const char* provider;
  const char* name;
  uint32_t arity; /* 1 or 2: number of operands in composition syntax. */
  uint32_t flags;
  X3TensorVisitor replay;
  void* context;
  void (*cleanup)(void*);
} X3TensorOperatorDef;

X3_API X3Status x3_tensor_create(X3Runtime*, X3TensorDType,
    const int64_t* shape, uint32_t rank, const void* data, uint64_t bytes, X3Value*);
/* Ownership is transferred only on success. cleanup runs once, after all views
   and graphs retaining this storage are released. */
X3_API X3Status x3_tensor_wrap(X3Runtime*, const X3TensorInfo*,
    void* owner, void (*cleanup)(void*), X3Value*);
X3_API X3Status x3_tensor_input(X3Runtime*, const char* name, X3TensorDType,
    const int64_t* shape, uint32_t rank, X3Value*);
X3_API X3Status x3_tensor_info(X3Runtime*, X3Value, X3TensorInfo*);
X3_API X3Status x3_tensor_view(X3Runtime*, X3Value,
    const int64_t* shape, const int64_t* strides, uint32_t rank, uint64_t offset, X3Value*);
/* Returns a callable factory: factory(op_name, **attributes) creates an operator.
   The factory/expressions retain the registration. cleanup ownership on success. */
X3_API X3Status x3_tensor_register_operator(X3Runtime*, const X3TensorOperatorDef*, X3Value*);
X3_API X3Status x3_tensor_apply(X3Runtime*, X3Value left, X3Value right,
    const char* intrinsic, X3Value*);
X3_API X3Status x3_tensor_graph(X3Runtime*, X3Value structured_outputs, X3Value*);
/* Bindings is a dict keyed by input name. Results preserve output containers.
   Execution state/buffers are per run; graph plans are reusable. */
X3_API X3Status x3_tensor_graph_run(X3Runtime*, X3Value graph, X3Value bindings, X3Value*);
/* A null visitor invokes the registered per-operator callbacks. */
X3_API X3Status x3_tensor_graph_replay(X3Runtime*, X3Value graph, X3TensorVisitor, void*);
X3_API X3Status x3_tensor_graph_inspect(X3Runtime*, X3Value graph, X3Value*);

#ifdef __cplusplus
}
#endif
#endif
