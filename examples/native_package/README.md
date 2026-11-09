# Publish and import a native Xlang3 package

This example uses only the Xlang3 checkout and its bundled YAML module. The
same `demo` shared library is imported by a C++ host and an Xlang3 script.

## Publish a C++ API

[demo.cpp](demo.cpp) declares the script-facing names `answer` and
`parse_yaml` with `BEGIN_PACKAGE` and `APISET().AddFunc<N>`, where `N`
is the number of arguments. `XLANG3_IMPLEMENT_PACKAGE(demo)` exports
`Load` and `xlang3_package_abi_version`. When Xlang3 finds the shared library
for `demo`, it checks the ABI and calls `Load` to install the API on the module.
Use SDK headers from the same Xlang3 build as the runtime.

The `parse_yaml` implementation shows how one native package imports
another. `X::Module yaml(Host(), "yaml")` imports into the runtime that
loaded `demo`. The package then calls `yaml.fn("loads")`.

## Create a runtime and import from C++

Include `xlang3/xlang3.h` from the SDK. Link the application with the
`xlang3_runtime` shared library and compile with `XLANG3_USE_SHARED`, as
shown in [CMakeLists.txt](CMakeLists.txt). Construct `X::Runtime` before
creating module and result values so it outlives them.

```cpp
X::Runtime runtime;
runtime.AddImportRoot(package_directory);
X::Module demo(runtime, "demo");

X::Value answer;
if (!demo.fn("answer").Call({}, answer)) {
  throw std::runtime_error(runtime.LastError());
}
std::cout << answer.ToInt64() << '\n';
```

Pass the directory containing the package file to `AddImportRoot`.
The name `"demo"` in `X::Module` is the import name. Xlang3 resolves it to
`demo.x3pkg.dll`, `demo.x3pkg.so`, or `demo.x3pkg.dylib` on the
respective platform. See [import_demo.cpp](import_demo.cpp) for the full
host, including error handling.

For a compatible Garnet installation, add the directory containing the Garnet
native library and use `X::Module garnet(runtime, "garnet")` in the same way.

## Import the host API from an embedded script

[run_host_script.cpp](run_host_script.cpp) follows the Cantor host pattern.

```cpp
CantorHost host;
X::Runtime runtime;
runtime.RegisterPackage("cantor", host);
runtime.EvalFile(script_path);
```

[use_host_api.py](use_host_api.py) runs inside that host process.

```python
import cantor

cantor.mark_ready()
```

1. `APISET().AddFunc<0>("mark_ready", &CantorHost::MarkReady)` exposes
   `MarkReady` under the script-facing name `mark_ready`.
2. `RegisterPackage("cantor", host)` puts a module named `cantor` in this
   runtime's module registry and binds the exposed method to the existing
   C++ object. The registration name is the name the script imports.
3. `EvalFile(script_path)` executes the script through that same runtime.
   `import cantor` finds the registered module there. If the host creates
   several runtimes in one process, register the API in the runtime that
   will execute the script.
4. `cantor.mark_ready()` calls `MarkReady` on the live C++ object in
   process. When `EvalFile` returns, C++ reads the updated object and prints
   `Host ready: true`.

`RegisterPackage` borrows the host object. Declare the object before the
runtime so C++ destroys the runtime first.

## Build and run

From the Xlang3 checkout, configure and build the runtime, CLI, and example.
The `xlang3_import_demo` target also builds the demo and YAML packages.

On Windows with a Visual Studio generator, run the following in PowerShell.

```powershell
cmake -S . -B build
cmake --build build --config Release --target xlang3 xlang3_import_demo xlang3_run_host_script
& ./build/Release/xlang3_import_demo.exe ./build/Release/native_package_demo
cmake -E env "PYTHONPATH=$PWD/build/Release/native_package_demo" `
  ./build/Release/xlang3.exe ./examples/native_package/use_demo.py
& ./build/Release/xlang3_run_host_script.exe ./examples/native_package/use_host_api.py
```

With a single-config generator on Linux or macOS, use the build directory
for the executables and pass the YAML output directory explicitly.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target xlang3 xlang3_import_demo xlang3_run_host_script
./build/xlang3_import_demo ./build/native_package_demo ./build/Release/modules
PYTHONPATH="$PWD/build/native_package_demo:$PWD/build/Release/modules" \
  ./build/xlang3 ./examples/native_package/use_demo.py
./build/xlang3_run_host_script ./examples/native_package/use_host_api.py
```

The two shared-library import examples print:

```text
42
{'answer': 42}
```
