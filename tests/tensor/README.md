# Tensor CPU Tests

These tests use XLang3's tensor graph and built-in CPU executor. They do not
require Garnet, CUDA, NumPy, or a GPU.

Build Release with the workspace's existing build script, then run:

```powershell
ctest --test-dir <build>/components/xlang3 -C Release -R "^xlang3_tensor_" --output-on-failure
```

## Coverage

- `cpu_tests.cpp`: public C ABI and C++ SDK, external storage and strided views,
  exactly-once cleanup, invalid descriptors, runtime ownership, unknown symbolic
  metadata, registered unary/binary replay, tensor-valued attributes, callback
  failures, a million-element four-dimensional tensor, and calls from four host
  threads. Host-thread safety does not imply parallel CPU kernel execution.
- `cpu_graph.py`: capture and repeated execution, nested outputs and tensor
  aliases, arithmetic and operator composition, broadcasting, reductions,
  reshape/permute, batched matmul, all four dtypes, empty dimensions, nested
  fusion annotations, immutable captured attributes, and failure cases.
- `cpu_reference.py`: 309 numerical comparisons against scalar Python calculations,
  including non-square and empty matrices, strided operands, three-dimensional
  broadcasting, reductions, scalar tensors, the 32-axis limit, and explicit rejection of symbolic
  data-dependent branching.

## Example

```python
import tensor as T

x = T.input("x", shape=[2, 3])
bias = T.tensor([1, 2, 3])
graph = T.graph((x + bias) * T.unary_op("relu"))
result = graph.run({"x": T.tensor([[1, 2, 3], [4, 5, 6]])})
assert result.tolist() == [2, 4, 6, 5, 7, 9]
```

Both CPU execution and external-backend replay consume the captured graph.
Registered binary composition `a * binary_op("name", **attrs) * b` produces
one operation with two operands. Replay visits dependencies before consumers;
ordered operations retain creation order. Only nodes reachable from the graph's
outputs are included: state/effect operations must remain in that dependency chain.

## Current Boundaries

CPU dtypes are float32, float64, int32, and int64. Kernels cover add/sub/mul/div,
neg, relu, exp, sum (all elements or one axis), reshape, permute, and rank-two-or-
higher batched matmul. Views support nonnegative byte strides. Expression shape
and dtype remain unknown until execution; named inputs have declared metadata.

Custom backend operations can be captured and replayed, but the CPU executor
rejects operations for which it has no kernel. These tests do not verify Garnet
compilation/execution, tensor serialization, negative strides, data-dependent
control flow, or full NumPy compatibility. Matmul currently uses built-in scalar
kernels, not a tuned BLAS implementation; no BLAS-equivalent performance claim
is made. Graph runs have independent result buffers, without a buffer-reuse plan.
