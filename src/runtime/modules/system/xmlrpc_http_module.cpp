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
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"

#include <string>
#include <vector>

namespace xlang3 {

namespace {

std::string as_string(const Value& value) {
  if (auto* string = value_as_string(value)) {
    return string_object_to_string(*string);
  }
  return value_to_string(value);
}

std::string xml_escape(const std::string& value) {
  std::string out;
  for (char ch : value) {
    switch (ch) {
    case '&':
      out += "&amp;";
      break;
    case '<':
      out += "&lt;";
      break;
    case '>':
      out += "&gt;";
      break;
    case '"':
      out += "&quot;";
      break;
    default:
      out.push_back(ch);
      break;
    }
  }
  return out;
}

std::string xml_unescape(std::string value) {
  const std::pair<const char*, const char*> replacements[] = {
      {"&lt;", "<"},
      {"&gt;", ">"},
      {"&quot;", "\""},
      {"&amp;", "&"},
  };
  for (const auto& replacement : replacements) {
    size_t pos = 0;
    const std::string from = replacement.first;
    const std::string to = replacement.second;
    while ((pos = value.find(from, pos)) != std::string::npos) {
      value.replace(pos, from.size(), to);
      pos += to.size();
    }
  }
  return value;
}

std::string xmlrpc_value(const Value& value) {
  if (value.tag == ValueTag::Bool) {
    return std::string("<value><boolean>") + (value.as.b ? "1" : "0") + "</boolean></value>";
  }
  if (value.tag == ValueTag::Int64) {
    return "<value><int>" + std::to_string(value.as.i64) + "</int></value>";
  }
  if (value.tag == ValueTag::Double) {
    return "<value><double>" + std::to_string(value.as.f64) + "</double></value>";
  }
  return "<value><string>" + xml_escape(as_string(value)) + "</string></value>";
}

std::string xml_between(const std::string& text, const std::string& begin, const std::string& end, size_t start = 0) {
  const size_t begin_pos = text.find(begin, start);
  if (begin_pos == std::string::npos) {
    return {};
  }
  const size_t content_pos = begin_pos + begin.size();
  const size_t end_pos = text.find(end, content_pos);
  if (end_pos == std::string::npos) {
    return {};
  }
  return text.substr(content_pos, end_pos - content_pos);
}

Value parse_xmlrpc_scalar(const std::string& body) {
  std::string value = xml_between(body, "<int>", "</int>");
  if (!value.empty()) {
    return Value::int64(std::stoll(value));
  }
  value = xml_between(body, "<i4>", "</i4>");
  if (!value.empty()) {
    return Value::int64(std::stoll(value));
  }
  value = xml_between(body, "<boolean>", "</boolean>");
  if (!value.empty()) {
    return Value::boolean(value == "1" || value == "true");
  }
  value = xml_between(body, "<double>", "</double>");
  if (!value.empty()) {
    return Value::number(std::stod(value));
  }
  value = xml_between(body, "<string>", "</string>");
  if (!value.empty()) {
    return Value::string(xml_unescape(value));
  }
  value = xml_between(body, "<value>", "</value>");
  return Value::string(xml_unescape(value));
}

bool xmlrpc_dumps_kw(
    Runtime&,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 1 || argc > 2) {
    error = "xmlrpc.client.dumps() expected params and optional methodname";
    return false;
  }
  std::string method_name;
  if (argc >= 2) {
    method_name = as_string(args[1]);
  }
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name != nullptr && std::string(kwargs[i].name) == "methodname" && kwargs[i].value != nullptr) {
      method_name = as_string(*kwargs[i].value);
    }
  }
  std::vector<Value> params;
  if (auto* tuple = value_as_tuple(args[0])) {
    params = tuple->items;
  } else if (auto* list = value_as_list(args[0])) {
    params = list->items;
  } else {
    params.push_back(args[0]);
  }
  std::string xml = "<?xml version=\"1.0\"?><methodCall>";
  if (!method_name.empty()) {
    xml += "<methodName>" + xml_escape(method_name) + "</methodName>";
  }
  xml += "<params>";
  for (const auto& param : params) {
    xml += "<param>";
    xml += xmlrpc_value(param);
    xml += "</param>";
  }
  xml += "</params></methodCall>";
  out = Value::string(std::move(xml));
  return true;
}

bool xmlrpc_dumps(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  return xmlrpc_dumps_kw(runtime, args, argc, nullptr, 0, out, error, user_data);
}

bool xmlrpc_loads(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "xmlrpc.client.loads() expected XML data";
    return false;
  }
  auto* string = value_as_string(args[0]);
  if (string == nullptr) {
    error = "xmlrpc.client.loads() expected str";
    return false;
  }
  const std::string xml = string_object_to_string(*string);
  std::vector<Value> params;
  size_t pos = 0;
  while ((pos = xml.find("<param>", pos)) != std::string::npos) {
    const size_t end = xml.find("</param>", pos);
    if (end == std::string::npos) {
      break;
    }
    params.push_back(parse_xmlrpc_scalar(xml.substr(pos, end - pos)));
    pos = end + 8;
  }
  const std::string method_name = xml_unescape(xml_between(xml, "<methodName>", "</methodName>"));
  out = Value::tuple({Value::tuple(std::move(params)), method_name.empty() ? Value::none() : Value::string(method_name)});
  return true;
}

bool trivial_init(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1) {
    error = "object initializer missing self";
    return false;
  }
  value_set_none(out);
  return true;
}

Value simple_class(Runtime& runtime, const std::string& name) {
  return Value::class_object(name, {{"__init__", runtime.make_native_function(name + ".__init__", trivial_init)}});
}

Value http_status_class() {
  return Value::class_object(
      "HTTPStatus",
      {
          {"OK", Value::int64(200)},
          {"CREATED", Value::int64(201)},
          {"NO_CONTENT", Value::int64(204)},
          {"MOVED_PERMANENTLY", Value::int64(301)},
          {"FOUND", Value::int64(302)},
          {"BAD_REQUEST", Value::int64(400)},
          {"UNAUTHORIZED", Value::int64(401)},
          {"FORBIDDEN", Value::int64(403)},
          {"NOT_FOUND", Value::int64(404)},
          {"INTERNAL_SERVER_ERROR", Value::int64(500)},
          {"BAD_GATEWAY", Value::int64(502)},
          {"SERVICE_UNAVAILABLE", Value::int64(503)},
      });
}

Value http_responses() {
  return Value::dict({
      {Value::int64(200), Value::string("OK")},
      {Value::int64(201), Value::string("Created")},
      {Value::int64(204), Value::string("No Content")},
      {Value::int64(301), Value::string("Moved Permanently")},
      {Value::int64(302), Value::string("Found")},
      {Value::int64(400), Value::string("Bad Request")},
      {Value::int64(401), Value::string("Unauthorized")},
      {Value::int64(403), Value::string("Forbidden")},
      {Value::int64(404), Value::string("Not Found")},
      {Value::int64(500), Value::string("Internal Server Error")},
      {Value::int64(502), Value::string("Bad Gateway")},
      {Value::int64(503), Value::string("Service Unavailable")},
  });
}

} // namespace

void register_xmlrpc_http_modules(Runtime& runtime) {
  NativeModuleBuilder xmlrpc(runtime, "xmlrpc");
  Value xmlrpc_module = xmlrpc.finish();

  NativeModuleBuilder xmlrpc_client(runtime, "xmlrpc.client");
  xmlrpc_client.value("ServerProxy", simple_class(runtime, "ServerProxy"))
      .value("Marshaller", simple_class(runtime, "Marshaller"))
      .value("Server", simple_class(runtime, "Server"))
      .value("Binary", simple_class(runtime, "Binary"))
      .value("DateTime", simple_class(runtime, "DateTime"))
      .function("loads", xmlrpc_loads)
      .value("dumps", runtime.make_native_function("xmlrpc.client.dumps", xmlrpc_dumps, nullptr, nullptr, nullptr, false, xmlrpc_dumps_kw));
  Value xmlrpc_client_module = xmlrpc_client.finish();

  NativeModuleBuilder xmlrpc_server(runtime, "xmlrpc.server");
  xmlrpc_server.value("SimpleXMLRPCServer", simple_class(runtime, "SimpleXMLRPCServer"))
      .value("SimpleXMLRPCRequestHandler", simple_class(runtime, "SimpleXMLRPCRequestHandler"));
  Value xmlrpc_server_module = xmlrpc_server.finish();
  std::string ignored;
  module_set_attr(xmlrpc_module, "client", xmlrpc_client_module, ignored);
  module_set_attr(xmlrpc_module, "server", xmlrpc_server_module, ignored);
  runtime.register_module("xmlrpc", xmlrpc_module);
  runtime.register_module("xmlrpc.client", xmlrpc_client_module);
  runtime.register_module("xmlrpc.server", xmlrpc_server_module);

  Value status = http_status_class();
  NativeModuleBuilder http(runtime, "http");
  http.value("HTTPStatus", status);
  Value http_module = http.finish();

  NativeModuleBuilder http_client(runtime, "http.client");
  http_client.value("HTTP_PORT", Value::int64(80))
      .value("HTTPS_PORT", Value::int64(443))
      .value("OK", Value::int64(200))
      .value("NOT_FOUND", Value::int64(404))
      .value("HTTPException", simple_class(runtime, "HTTPException"))
      .value("HTTPConnection", simple_class(runtime, "HTTPConnection"))
      .value("HTTPSConnection", simple_class(runtime, "HTTPSConnection"))
      .value("HTTPResponse", simple_class(runtime, "HTTPResponse"))
      .value("responses", http_responses());
  Value http_client_module = http_client.finish();

  NativeModuleBuilder http_server(runtime, "http.server");
  http_server.value("BaseHTTPRequestHandler", simple_class(runtime, "BaseHTTPRequestHandler"))
      .value("SimpleHTTPRequestHandler", simple_class(runtime, "SimpleHTTPRequestHandler"))
      .value("HTTPServer", simple_class(runtime, "HTTPServer"))
      .value("ThreadingHTTPServer", simple_class(runtime, "ThreadingHTTPServer"));
  Value http_server_module = http_server.finish();
  module_set_attr(http_module, "client", http_client_module, ignored);
  module_set_attr(http_module, "server", http_server_module, ignored);
  runtime.register_module("http", http_module);
  runtime.register_module("http.client", http_client_module);
  runtime.register_module("http.server", http_server_module);
}

} // namespace xlang3
