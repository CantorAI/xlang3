# Hosting shared-memory LRPC from the C++ SDK

The C++ SDK can now expose a native package through xlang3's shared-memory
LRPC transport:

```cpp
X::Runtime runtime;
Service service;
auto package = runtime.RegisterPackage("service", service);
runtime.RegisterRemoteObject("service", package);
runtime.LrpcListen(port);
```

`RegisterRemoteObject` requires a value owned by the same runtime. The listener
is started without blocking the caller and remains tied to that runtime;
destroying the runtime detaches its listener and registered values. A process
may own one listener runtime and one endpoint at a time. The C ABI exposes the
same registration and listen calls for hosts that use `X3Runtime` directly.

The endpoint is useful for native worker communication, but its name or port is
not an authorization boundary. A supervising application must bind each
connection to the launched process identity and its own per-run authority
before allowing a worker to access durable state, credentials, or tools.
LRPC only supplies transport and value marshaling; it does not provide that
application-level policy.

The `xlang3_ipc_native_host_tests` contract registers a C++ package, starts the
native LRPC listener, calls it through a second C++ runtime, verifies string
and integer round trips, probes host identity, and rejects invalid ports and
cross-runtime values before dispatch.
