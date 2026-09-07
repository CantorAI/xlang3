#ifndef XLANG3_ABI_TENSOR_H
#define XLANG3_ABI_TENSOR_H
#include "xlang3/abi/xapi.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum X3TensorDType {
  X3_TENSOR_FLOAT32 = 1, X3_TENSOR_FLOAT64 = 2,
  X3_TENSOR_INT32 = 3, X3_TENSOR_INT64 = 4,
  /* Additional storage/capture formats for external backends. CPU arithmetic
     is not implemented for these types. Floating formats retain their packed
     IEEE binary16, bfloat16, or named FP8 encodings without conversion. */
  X3_TENSOR_FLOAT16 = 5, X3_TENSOR_BFLOAT16 = 6,
  X3_TENSOR_UINT16 = 7,
  X3_TENSOR_FLOAT8_E4M3FN = 8, X3_TENSOR_FLOAT8_E4M3FNUZ = 9,
  X3_TENSOR_FLOAT8_E5M2 = 10, X3_TENSOR_FLOAT8_E5M2FNUZ = 11
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

/* Access ordering belongs to storage, including every view. Info() does not
   acquire access. Native clients must bracket payload access with a use lease.
   READ leases may overlap; WRITE excludes other leases until end_use publishes
   completion. Acquire one WRITE for in-place operations (do not nest a READ on
   the same storage). Acquire multiple storages in a consistent caller order.
   Leases retain storage, not the runtime, and may be ended on another thread. */
typedef struct X3TensorUse X3TensorUse;
typedef enum X3TensorAccess { X3_TENSOR_READ = 1, X3_TENSOR_WRITE = 2 } X3TensorAccess;
typedef struct X3TensorExecution {
  uint32_t size;
  int32_t device_type; /* 0 requests a blocking host wait. */
  int32_t device_id;
  void* stream; /* Backend-defined; borrowed only during begin_use. */
} X3TensorExecution;
typedef struct X3TensorCompletion {
  uint32_t size;
  void* context;
  /* Enqueue a dependency on execution, or block for host/unsupported execution.
     A host wait must quiesce access even when reporting a computation failure.
     Callbacks must be thread-safe, nonthrowing and must not re-enter this storage. */
  X3Status (*wait)(void*, const X3TensorExecution*);
  /* Nonblocking: 1 completed, 0 pending, -1 failed. Required for bounded tracking. */
  int32_t (*query)(void*);
  /* Must ensure no work still touches storage, even after wait/query errors.
     Called exactly once, before storage owner cleanup. Must not throw. */
  void (*cleanup)(void*);
} X3TensorCompletion;
X3_API X3Status x3_tensor_begin_use(X3Runtime*, X3Value, X3TensorAccess,
    const X3TensorExecution*, X3TensorUse**);
typedef struct X3TensorUseRequest {
  X3Value tensor;
  X3TensorAccess access;
} X3TensorUseRequest;
/* Deduplicates views by storage, promotes aliases to WRITE if any request
   writes, and orders storage acquisition consistently. Use this for operations
   with multiple tensors; do not mix nested single/batch acquisitions. */
X3_API X3Status x3_tensor_begin_uses(X3Runtime*, const X3TensorUseRequest*, uint32_t count,
    const X3TensorExecution*, X3TensorUse**);
/* null completion means synchronous access has finished. Transfers completion
   ownership and consumes the lease only on success. No runtime entry required. */
X3_API X3Status x3_tensor_end_use(X3TensorUse*, const X3TensorCompletion*);

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
/* Non-throwing type query; does not set the runtime error on non-tensors. */
X3_API int32_t x3_tensor_is_tensor(X3Value);
X3_API int32_t x3_tensor_is_graph(X3Value);
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
/* Returns a container snapshot, retaining tensor references without copying
   their payloads. Tensor aliases and output container structure are preserved. */
X3_API X3Status x3_tensor_graph_outputs(X3Runtime*, X3Value graph, X3Value*);

#ifdef __cplusplus
}
#endif
#endif
