import tensor as T


def same(tensor, shape, values):
    assert tensor.shape == shape, (tensor.shape, shape)
    assert tensor.tolist() == values, (tensor.tolist(), values)


def fails(fn):
    failed = False
    try:
        fn()
    except Exception:
        failed = True
    assert failed


a = T.tensor([[1, 2, 3], [4, 5, 6]])
print("tensor-created", flush=True)
b = T.input("b", shape=[3])
expression = (a + b) * T.unary_op("relu")
graph = T.graph({"value": expression, "alias": [expression, a]})
print("tensor-graph-built", flush=True)
for i in range(10):
    result = graph.run({"b": T.tensor([10, 20, 30])})
    same(result["value"], (2, 3), [11, 22, 33, 14, 25, 36])
    assert result["value"] is result["alias"][0]
    assert result["alias"][1] is a
print("tensor-repeated-run-passed", flush=True)

same((10 - a).eval(), (2, 3), [9, 8, 7, 6, 5, 4])
same((-a).eval(), (2, 3), [-1, -2, -3, -4, -5, -6])
same((a / 2).eval(), (2, 3), [0.5, 1, 1.5, 2, 2.5, 3])
print("tensor-arithmetic-passed", flush=True)
same((a * T.binary_op("add") * 3).eval(), (2, 3), [4, 5, 6, 7, 8, 9])
same((3 * T.binary_op("sub") * a).eval(), (2, 3), [2, 1, 0, -1, -2, -3])
assert (a + a).shape is None
assert (a + a).dtype is None
assert len(T.graph(a * T.binary_op("add") * 3).inspect()) == 2
same((a * T.unary_op("sum")).eval(), (), [21])
same((a * T.unary_op("sum", axis=0)).eval(), (3,), [5, 7, 9])
same((a * T.unary_op("sum", axis=-1)).eval(), (2,), [6, 15])

transposed = (a * T.unary_op("permute", axes=[1, 0])).eval()
same(transposed, (3, 2), [1, 4, 2, 5, 3, 6])
same((transposed + 1).eval(), (3, 2), [2, 5, 3, 6, 4, 7])
same((a * T.unary_op("reshape", shape=[3, 2])).eval(), (3, 2), [1, 2, 3, 4, 5, 6])
same((a @ transposed).eval(), (2, 2), [14, 32, 32, 77])
same((a * T.binary_op("matmul") * transposed).eval(), (2, 2), [14, 32, 32, 77])
batch = T.tensor([[[1, 0], [0, 1]], [[2, 0], [0, 2]]])
same((batch @ T.tensor([[1, 2], [3, 4]])).eval(), (2, 2, 2), [1, 2, 3, 4, 2, 4, 6, 8])
print("tensor-layout-matmul-passed", flush=True)

for dtype in [T.float32, T.float64, T.int32, T.int64]:
    x = T.tensor([1, 2, 3], dtype=dtype)
    assert (x + x).eval().dtype == dtype
    same((x * 2).eval(), (3,), [2, 4, 6])
big = T.tensor([9007199254740993], dtype=T.int64)
same((big + 1).eval(), (1,), [9007199254740994])
same((T.tensor([], shape=[0, 3]) + T.tensor([1, 2, 3])).eval(), (0, 3), [])
same((T.tensor([], shape=[2, 0]) * T.unary_op("sum", axis=1)).eval(), (2,), [0, 0])
same((T.tensor([], shape=[2, 0]) @ T.tensor([], shape=[0, 3])).eval(), (2, 3), [0, 0, 0, 0, 0, 0])
print("tensor-dtypes-empty-passed", flush=True)

# Capture follows configuration loops; nested fusion returns expressions until
# the root call constructs the graph. No CPU data is allocated for these nodes.
@T.fusion(role="layer", atomic=True)
def layer(x, bias):
    return (x + bias) * T.unary_op("relu")


@T.fusion(name="cpu_model", boundary="required")
def model(x, bias, depth):
    for i in range(depth):
        x = layer(x, bias)
    return {"last": x, "outputs": [x]}


x = T.input("x", shape=[3])
model_graph = model(x, b, 3)
model_output = model_graph.run({"x": T.tensor([1, 2, 3]), "b": T.tensor([1, 1, 1])})
same(model_output["last"], (3,), [4, 5, 6])
assert model_output["last"] is model_output["outputs"][0]
assert len(model_graph.inspect()[-1]["regions"]) == 2
print("tensor-fusion-passed", flush=True)

# Tensor-valued kwargs must be visible dependencies, not discarded attributes.
dep = x + 1
nodes = T.graph(x * T.unary_op("relu", extra={"tensor": [dep]})).inspect()
assert nodes[-1]["inputs"] == [x.id, dep.id]
params = [3]
reshape = x * T.unary_op("reshape", shape=params)
params[0] = 999
same(T.graph(reshape).run({"x": T.tensor([1, 2, 3])}), (3,), [1, 2, 3])

fails(lambda: graph.run({}))
fails(lambda: graph.run({"b": T.tensor([1])}))
fails(lambda: (a + T.tensor([1, 2])).eval())
fails(lambda: (a / 0).eval())
fails(lambda: T.tensor([[1], [2, 3]]))
fails(lambda: T.tensor(shape=[-1]))
fails(lambda: T.tensor(shape=[9223372036854775807, 2]))
fails(lambda: (a * T.unary_op("permute", axes=[0, 0])).eval())
fails(lambda: (transposed * T.unary_op("reshape", shape=[6])).eval())
fails(lambda: (a * T.unary_op("sum", axis=3)).eval())
fails(lambda: (a * T.unary_op("unknown_backend_operation")).eval())
fails(lambda: (a * T.unary_op("sum", axiss=1)).eval())
fails(lambda: (a * T.unary_op("relu", extra=1)).eval())
fails(lambda: bool(x))
fails(lambda: bool(a))
fails(lambda: T.graph(x * T.binary_op("add")))
fails(lambda: T.graph([x, T.input("x", shape=[3])]))
fails(lambda: (T.tensor([9223372036854775807], dtype=T.int64) + 1).eval())
fails(lambda: (T.tensor([-9223372036854775808], dtype=T.int64) * -1).eval())
cyclic = []
cyclic.append(cyclic)
fails(lambda: x * T.unary_op("relu", extra=cyclic))
print("tensor-cpu-graph-passed")
