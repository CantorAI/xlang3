/*
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
*/
#include "xlang3/builtins.h"

#include "xlang3/module_object.h"

namespace xlang3 {

void register_xmlrpc_http_modules(Runtime& runtime) {
  NativeModuleBuilder xmlrpc(runtime, "xmlrpc");
  runtime.register_module("xmlrpc", xmlrpc.finish());

  NativeModuleBuilder xmlrpc_client(runtime, "xmlrpc.client");
  xmlrpc_client.value("ServerProxy", Value::class_object("ServerProxy", {}))
      .value("Marshaller", Value::class_object("Marshaller", {}))
      .value("Server", Value::class_object("Server", {}));
  runtime.register_module("xmlrpc.client", xmlrpc_client.finish());

  NativeModuleBuilder xmlrpc_server(runtime, "xmlrpc.server");
  xmlrpc_server.value("SimpleXMLRPCServer", Value::class_object("SimpleXMLRPCServer", {}));
  runtime.register_module("xmlrpc.server", xmlrpc_server.finish());

  NativeModuleBuilder http(runtime, "http");
  runtime.register_module("http", http.finish());

  NativeModuleBuilder http_server(runtime, "http.server");
  http_server.value("BaseHTTPRequestHandler", Value::class_object("BaseHTTPRequestHandler", {}))
      .value("HTTPServer", Value::class_object("HTTPServer", {}));
  runtime.register_module("http.server", http_server.finish());
}

} // namespace xlang3
