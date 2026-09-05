# Value Serialization

The C ABI provides `x3_value_to_bytes` / `x3_value_from_bytes` and
`x3_value_to_stream` / `x3_value_from_stream`. C++ `X::Value` and `X::Stream`
wrap those same APIs. Streams use the runtime's existing block stream; strings
and binary data are written from their existing storage without a temporary
whole-value string.

## Graphs and Code

The graph format preserves aliases and cycles. Supported values include scalar
values, strings, bytes, bytearrays, lists, tuples, dictionaries, captured resource
expressions, script functions, closure cells, classes, instances, bound methods,
properties, static/class methods, and slot descriptors. Memoryviews of bytes or
bytearrays restore as bytes, retaining the earlier binary-copy semantics.

Functions carry compiled IR, defaults, annotations, attributes, shared closure
cells, and a snapshot of globals referenced by their code and nested functions.
Global slot positions are preserved. Calls to `globals`, `eval`, or `exec`
conservatively capture the entire namespace. The receiver does not
need the producer's script file. Classes carry their bases, metaclass, methods,
attributes, and slot layout. Restore allocates objects before connecting edges;
it does not rerun class bodies or instance constructors.

Imported modules and registered native symbols resolve by name on the receiver.
Their matching dependencies must be installed there. An unsupported object in
the captured graph causes an explicit error. Live generators, files, remote proxies, arbitrary native pointers, and
other unlisted runtime object kinds are not transferable by this format.

## Native State

Native functions can opt into argument snapshots over IPC by setting
`X3_NATIVE_IPC_ARGS_BY_VALUE` in `X3NativeFunctionDef.flags`. The C++ equivalent is
`APISET().AddFunc<1>("__call__", &TaskDecorator::Bind, X3_NATIVE_IPC_ARGS_BY_VALUE)`.
The positional and keyword arguments form one graph, preserving shared references.
Serialization writes to the transport stream directly. Local calls are unchanged;
unmarked IPC calls retain ordinary remote-reference semantics. Both endpoints must
support this operation. Use it only for trusted callers, just like graph restore.

Register `X3NativeSerializerDef` using the C API or the package host table:

- `type_id` is the stable wire identifier; `native_type` is the local type name
  used by `instance_set_native_data`.
- `version` identifies the state contract. Missing codecs and version mismatches
  fail explicitly.
- `encode` returns an owned ordinary value containing native state. Values in
  this state participate in the same graph, preserving shared references.
- `decode` receives a preallocated instance and borrowed state. It attaches native
  data with its cleanup callback, without invoking the instance constructor.
- Callbacks must not throw across the C ABI or execute the partially restored
  graph. On failure, attached native data is cleaned up. Registration takes
  ownership of codec userdata only on success.

See [the native test package](../tests/serialize/native_package.cpp) for a complete
DLL example using the C++ package API with C ABI serialization callbacks.

## Lifetime and Trust

The runtime retains restored objects to collect cyclic graphs. Collection runs
before another restore and can be requested with
`x3_runtime_collect_serialized_objects` / `X::Runtime::CollectSerializedObjects()`.
Externally held values remain live. Release values before destroying their runtime.
References hidden inside opaque native data are conservatively treated as roots;
opaque native self-cycles require explicit release by their owner. Runtime teardown
clears remaining restored graphs before unloading native packages.

Only deserialize trusted data. Imports and native restore callbacks can execute
code; subsequently calling restored functions also executes code. The IR checksum
detects accidental corruption, not malicious input. This is neither a sandbox nor
a stable cross-version persistence or CPython pickle format.

The current format is graph version 1, IR version 14, SDK ABI 21. Both endpoints
must use compatible runtimes and native packages. Graph input/output is limited to
1 GiB, one million objects, and sixteen million fields per field vector.

Shared-memory IPC transport and its existing remote-reference policy are unchanged.
Executable by-value transfer uses these explicit serialization APIs; it does not
replace remote object handles automatically.

## Tests

`tests/serialize/graph_process_tests.cpp` runs separate producer and consumer
processes. The producer's temporary script is deleted before the consumer restores
and executes functions, closures, classes, inherited methods, and native state.
Tests also cover binary data, identity, cycles, malformed input, codec mismatches,
callback failures, and native cleanup.
