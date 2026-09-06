# CPython Bridge

Optional CPython 3.14 extension, imported as `xlang3`. Enable the CMake option
`XLANG3_BUILD_CPYTHON_BRIDGE` and build `xlang3_cpython`. The extension is emitted
under the build's configuration-specific `cpython_bridge` directory. Add that
directory to CPython's module search path. Windows builds place the matching
XLang3 runtime DLL beside it.

```python
import xlang3

module = xlang3.importModule("my_module", fromPath="path/to/scripts")
http = xlang3.importModule("http", fromPath="xlang_net")
cantor = xlang3.importModule("cantor", thru="lrpc:1000")
```

`fromPath` accepts a script search directory or a native package name. Native
packages must be available on XLang3's import search paths. `thru` delegates to
XLang3's shared-memory IPC importer. These imports execute in XLang3, not CPython.
Ordinary CPython imports are unchanged.

This entry point owns an XLang3 runtime, never the hosting CPython
interpreter. Returned object proxies retain that runtime and preserve object
identity. Calls, keyword arguments, property reads/writes, indexing, and length
are supported. Scalars, UTF-8 strings (including embedded NULs), bytes, and
existing XLang3 proxies can be passed as arguments. Strings and bytes are copied
between engines; this is not the zero-copy buffer bridge.
Integer arguments currently use the C ABI's signed 64-bit range and reject
overflow. Execution uses XLang3's runtime lock, released during blocking IPC and
native callbacks. Multiple Python callers and reentrant callbacks are tested;
performance parity has not been benchmarked.

Python containers, functions, classes, instances, and iterators are represented
as live native objects through the public XLang3 C ABI. They are not recursively
copied into XLang3 containers. Passing one back to Python returns the original
object, including cyclic containers. Attribute access and mutation, calls with
keywords, indexing and mutation, iteration, length, and truth testing delegate
to CPython. Runtime-held references keep Python objects alive; the bridge's GC
owner exposes those references to CPython's cycle collector.

Python callbacks can call XLang3 again, including a nested shared-memory remote
call. Common Python exception types are mapped for XLang3 exception handlers.
Failures escaping through the XLang3 C ABI currently become `RuntimeError` in
CPython; original traceback fidelity is not implemented.

CPython subinterpreters and full foreign-object protocol coverage are not
supported. Live objects are distinct from the explicit buffer and snapshot APIs
below.

## Buffers

`xlang3.buffer(value)` exposes a contiguous Python buffer to XLang3 without
copying its data. It retains the exporting object and its buffer lease. Writable
views permit changes in either engine; readonly views remain readonly. A pinned
bytearray cannot be resized until its exports are released. Noncontiguous
buffers are rejected rather than silently copied.

XLang3 buffer proxies also support Python's `memoryview`. Native one-dimensional
element formats are preserved in this direction. Exporting a Python buffer with
`xlang3.buffer` creates a raw byte view; it does not transfer multidimensional
shape or stride metadata. Ordinary bytes and string arguments still use copying
value conversion.

## Object Snapshots

```python
blob = xlang3.dumps(value)
restored = xlang3.loads(blob, trusted=True)
```

Snapshots use the public `X3Stream` C ABI. The CPython graph adapter has no
third-party serialization dependency. XLang3 proxies use XLang3's existing value
graph serializer; Python objects use a separate CPython graph format. Snapshot
bytes have an envelope identifying the format and exact CPython build version.
Earlier cloudpickle-format snapshots are rejected.

The CPython format preserves repeated references and container cycles. Supported
values include arbitrary-size integers, float/complex, Unicode, bytes,
bytearray, list/tuple/dict/set, closure cells, Python functions, ordinary Python
classes and instances, bound methods, static/class methods, and properties.
Frozenset elements are restored before hashing, including user-defined objects.
Empty and shared closure cells are preserved, including recursive functions and `__class__` cells
used by zero-argument `super()`.

Importable functions/classes and modules are restored by module and qualified
name. Those dependencies must be installed in the receiving interpreter. Local
functions are restored from CPython code objects using CPython's marshal API,
with defaults, keyword defaults, annotations, attributes, closure cells, and
referenced globals and each function's builtin dictionary. Functions sharing a
globals dictionary continue to share it, even when their builtin dictionaries differ.
Global names reached only through dynamic lookup such as `globals()[name]` are
not discovered by bytecode name analysis. Local classes are reconstructed with
their metaclass, bases, slots, methods, and attributes; instance state uses
`__getstate__`/`__setstate__` or dictionary/slot restoration. Module source is not
re-executed to recreate local definitions. This does not translate CPython code
into XLang3 code.

Class reconstruction supplies the saved namespace to the restored metaclass,
including `__classcell__` for methods using `super()`. Metaclass
`__prepare__`, `__new__`, `__init__`, descriptor `__set_name__`, and inherited
`__init_subclass__` hooks execute during reconstruction. These hooks may have
side effects, so loading remains an explicitly trusted operation. Original
class-statement keyword arguments are not retained by CPython. Classes requiring
such arguments can register a `copyreg` reducer for their metaclass to supply an
explicit reconstruction recipe; the codec supports those class reducers too.

The writer discovers object identities before emitting records. The reader
validates record framing and references, allocates object shells, then connects
their state. Bytes and bytearrays are read into their final allocation; encoding
writes their existing storage to the stream and copies the completed stream
directly into the returned snapshot bytes. There is no intermediate whole-graph
string or second payload bytes object. Snapshotting is not an atomic operation
with respect to user hooks or concurrent mutations.

Loading is executable and requires explicit trust. It can import modules,
construct classes, invoke state/descriptor hooks, and restore executable code.
`trusted=True` is not a sandbox. Never use it for untrusted files or messages.
Snapshots are not automatically decoded during IPC. The graph limits are
1,000,000 nodes, 8,000,000 references, 512 MiB of encoded graph data, and a maximum
constructor/key dependency depth of 512.

Native extension objects can supply `__reduce_ex__(4)` / `__reduce__`, or a
`copyreg` reducer. The codec records the constructor, arguments, state, item
iterators, and optional explicit state setter as graph references. It does not
copy native instance memory. Constructors may be imported or reconstructed local
functions, classes, bound methods, and callable instances. Before calling one,
the reader restores its callable and argument dependencies, including closures,
class methods/descriptors, and native object state. Completed dependencies are
not restored again, so constructor mutations are preserved. A readiness check
rejects genuine constructor cycles that would execute against unfinished
objects. Aliases and self-references through state/items are
preserved, and an explicit state setter takes precedence over `__setstate__`.
Importable global-name reductions are supported. Item iterators are consumed
with reference limits; open resources without a valid reducer remain unsupported.

Unsupported objects raise errors, not string or repr substitutes. Iterators with
a valid reducer preserve their supported state, including list iterator position
and references to their backing list. Active generator frames, locks, open
resources without a reconstruction recipe, and mixed Python/XLang3 graphs are
not supported by the CPython snapshot codec.
Hash-dependent cyclic state hooks and cyclic constructor dependencies are also
outside its supported restoration contract. These limits
do not prevent ordinary live calls to Python native extension objects.

## XLang3-Hosted CPython

The same optional build produces the native package `xlang_cpython.x3pkg`.
With its directory on XLang3's module search path:

```python
import cpython

hashes = cpython.importModule("_sha2")
print(hashes.sha256(b"abc").hexdigest())
application = cpython.importModule("my_python_module", "path/to/python/scripts")
```

The optional second argument adds a directory to CPython's shared `sys.path`;
CPython's normal `sys.modules` cache applies. Ordinary XLang3 imports are not
redirected to CPython. Imported Python objects use the same live-object bridge
and accept XLang3 callbacks, including callbacks made by Python worker threads.
The native package borrows its hosting XLang3 runtime, rather than creating a
second runtime. C++ SDK calls use the same C ABI path.

`cpython.buffer(value)`, `cpython.dumps(value)`, and `cpython.loads(data, True)`
use the same buffer and trusted snapshot implementations as the CPython module.

CPython initializes on the first package load, without installing signal handlers.
An already initialized CPython interpreter is reused. The interpreter is
process-owned and is deliberately not finalized when an XLang3 runtime unloads:
other runtimes and extension modules may still retain Python state. Python
`atexit` handlers are consequently not run by package teardown. Applications
must explicitly close their Python resources and join their Python worker threads.
Retained XLang3 proxies are invalidated during host teardown; subsequent calls
raise an error instead of accessing a destroyed runtime.

The build records its CPython executable and base installation directory as
defaults. For deployment, set `XLANG3_PYTHON_EXECUTABLE` and
`XLANG3_PYTHON_HOME` before the first import to use another compatible 3.14
installation. The native library still requires the matching CPython shared
library. Windows CMake builds stage that DLL beside the XLang3 executable;
the standard library and third-party packages remain in the Python installation.

Only one runtime per process can own the IPC export listener. Normal runtime
teardown drains listener workers and releases exported objects before destroying
the runtime. Shutdown waits for active requests; callbacks must finish. Unloading
the bridge from inside its own active remote callback is not a supported lifecycle.

Existing runtime limits observed by these tests: shared-memory IPC messages must
fit the session's 32 x 64 KiB slots, including protocol overhead. Larger requests
raise an error. The native JSON module currently truncates embedded NULs through
its older string API; direct bridge string round-trips preserve them. Neither
limitation is corrected by this bridge.

Tests are in `tests/cpython_bridge`: imports, live objects and callbacks, cyclic
lifetimes, IPC using a separate XLang3 server process, hosted CPython extension
imports, and C++ SDK calls across repeated runtime teardown. Release builds have
been tested on Windows with CPython 3.14.7. Linux and macOS are not yet verified.
IPC tests cover concurrent binary requests from zero bytes through 1 MiB,
including slot boundaries, nested callbacks, and recovery after oversized requests.
Buffer, protocol, and snapshot tests also cover binary sizes through 4 MiB,
shared cells, recursive functions, class state/descriptors, malformed records,
and restoration in a fresh CPython process started without site-packages.
Native reducer tests cover arrays, datetime/timezone, Decimal, mappings, partial
functions, regex, native container cycles, constructor state preservation,
slots, `copyreg`, and explicit state setters in both bridge directions.
Reducer dependency tests also cover local lambdas/classes, recursive closures,
callable instances, bound methods, stateful arguments, descriptor initialization,
constructor exceptions, and fresh-process restoration without module source.
Class tests cover local metaclass identity, class-creation hooks, explicit
keyword reconstruction recipes, and functions with different builtin dictionaries.
