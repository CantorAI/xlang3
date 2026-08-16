<!--
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
-->
# XLang3 RPC And Device Package

Status: architecture checkpoint.

User-facing API:

```python
import device

dev = device.connect()
print(dev.info())
print(dev.ping())
print(dev.echo("abc"))

gpio = dev.import_module("gpio")
led = gpio.Pin(15, gpio.OUT)
led.write(1)
```

`device` is generic. It is not named after Pico or any board. Board support is a
driver behind the device manager.

## Host Side

```text
xlang3.exe
  interactive Python-compatible REPL when no file is passed

modules/device/
  device.connect()
  Device
    info()
    put_file(path, data, persist=False)
    get_file(path)
    list_files(path)
    delete_file(path)
    import_module(name)
    exec(source)
    eval(expr)
    reset()
  RemoteObjectProxy
    __getattr__
    __setattr__
    __call__
    release()
```

The host REPL is local. Remote access happens only through the `device` package.

Current checkpoint:

```python
import device

dev = device.connect()
print(dev.info())
print(dev.exec("print(1 + 2)"))
print(dev.ping())
print(dev.echo("abc"))
dev.put_data("main.py", "print(123)", dev.ram)
print(dev.list_files("", dev.ram))
print(dev.get_data("main.py", dev.ram))
```

On Windows, `device.connect()` first checks `XLANG3_DEVICE_PORT`, then tries the
current Pico bring-up default `COM5`. The transport should move to proper USB
device discovery before this becomes production behavior.

File copy uses destination-first order:

```python
dev.put_data(remote_path, data, dev.ram)
dev.get_data(remote_path, dev.ram)

dev.put_file(remote_path, local_path, dev.ram)
dev.get_file(remote_path, local_path, dev.ram)

dev.delete_file(remote_path, dev.ram)
dev.list_files("", dev.ram)
```

`dev.flash` is the reserved persistent store selector. It is exposed now, but
returns a clear not-implemented error until the RP2040 flash store is added.

## Device Side

```text
ports/rp2040/
  embedded/
    rpc_server.cpp/.h
    protocol_codec.cpp/.h
    object_table.cpp/.h
    module_loader.cpp/.h
  fs/
    memory_file_store.cpp/.h
    flash_file_store.cpp/.h
  modules/
    gpio_module.cpp/.h
    time_module.cpp/.h
    console_module.cpp/.h
```

The device receives RPC frames, executes against the device runtime, and sends
structured responses. The device owns real objects. The host owns proxy handles.

## Protocol Operations

```text
HELLO
INFO
PING
RESET

PUT_FILE path, bytes, store
GET_FILE path, store
LIST_FILES path, store
DELETE_FILE path, store

IMPORT_MODULE name
GET_ATTR object_id, name
SET_ATTR object_id, name, value
CALL object_id, args, kwargs
CALL_METHOD object_id, name, args, kwargs
RELEASE object_id

EXEC source
EVAL expr
```

Structured object operations are the normal fast path. `EXEC` and `EVAL` exist
for deployment and convenience, not for every proxy call.

## Value Model

RPC values use XLang3-compatible encoding:

```text
None
bool
int64
double
string
bytes
list
tuple
dict
remote_ref
error
```

Large or stateful device-side values should return `remote_ref`.

## Remote Objects

```text
host RemoteObjectProxy
  object_id
  connection/session

device object table
  object_id -> X::Value
```

Lifetime:

```text
proxy created -> device object table retains X::Value
proxy release/GC -> RELEASE object_id
connection close -> release all session objects
```

## File Stores

The device supports both RAM and flash storage behind one C-style table:

```text
MemoryFileStore
  fast
  temporary
  used for development and tests

FlashFileStore
  persistent
  used for deploy and boot app
```

Import search order:

```text
static native modules
/ram
/flash
```

This lets development override flash without rewriting flash on every edit.

## RP2040 First Checkpoint

The first RP2040 RPC server may keep a temporary line command:

```text
py <one-line-source>
```

The final direction is framed binary RPC using the shared `src/rpc` protocol
types.
