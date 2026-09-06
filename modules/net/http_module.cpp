/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0
*/
#include "http_module.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace xlang_net {

HttpRequest::HttpRequest(const httplib::Request* request) : request_(request) {}

X::Value HttpRequest::GetMethod() const {
  return X::Value::String(Host(), request_ == nullptr ? "" : request_->method);
}

X::Value HttpRequest::GetBody() const {
  if (request_ == nullptr) return X::Value::String(Host(), "");
  if (request_->body.empty()) {
    auto list = X::Value::List(Host());
    for (const auto& item : request_->files) {
      auto data = X::Value::Dict(Host());
      data.Set("name", X::Value::String(Host(), item.second.name));
      data.Set("filename", X::Value::String(Host(), item.second.filename));
      data.Set("content_type", X::Value::String(Host(), item.second.content_type));
      data.Set("content", X::Value::Bytes(Host(), item.second.content.data(), item.second.content.size()));
      list.Append(data);
    }
    return list;
  }
  return is_text_type(request_->headers)
      ? X::Value::String(Host(), request_->body)
      : X::Value::Bytes(Host(), request_->body.data(), request_->body.size());
}

X::Value HttpRequest::GetPath() const {
  return X::Value::String(Host(), request_ == nullptr ? "" : request_->path);
}

X::Value HttpRequest::GetRemoteAddr() const {
  return X::Value::String(Host(), request_ == nullptr ? "" : request_->remote_addr);
}

X::Value HttpRequest::GetAllHeaders() const {
  return request_ == nullptr ? X::Value::Dict(Host()) : headers_to_dict(Host(), request_->headers);
}

X::Value HttpRequest::GetParams() const {
  auto dict = X::Value::Dict(Host());
  if (request_ == nullptr) return dict;
  for (const auto& item : request_->params) {
    dict.Set(item.first.c_str(), X::Value::String(Host(), item.second));
  }
  return dict;
}

HttpResponse::HttpResponse(httplib::Response* response) : response_(response) {}

bool HttpResponse::SetContent(X::Value value, std::string content_type) {
  if (response_ == nullptr) return false;
  uint64_t size = 0;
  if (const auto* data = static_cast<const char*>(value.BytesData(&size))) {
    response_->set_content(std::string(data, data + size), content_type.c_str());
    return true;
  }
  response_->set_content(value.ToString(false), content_type.c_str());
  return true;
}

bool HttpResponse::AddHeader(std::string name, X::Value value) {
  if (response_ == nullptr) return false;
  response_->set_header(name, value.ToString(false));
  return true;
}

bool HttpResponse::StreamFile(std::string file_path, long long start, long long end, std::string content_type) {
  if (response_ == nullptr || start < 0) return false;
  std::ifstream file(file_path, std::ios::binary);
  if (!file) return false;
  std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  if (data.empty()) {
    response_->set_content("", content_type.c_str());
    return true;
  }
  const long long last = end < 0 || end >= static_cast<long long>(data.size()) ? static_cast<long long>(data.size()) - 1 : end;
  if (start > last) return false;
  response_->set_content(data.substr(static_cast<size_t>(start), static_cast<size_t>(last - start + 1)), content_type.c_str());
  return true;
}

bool HttpResponse::StreamFileWithCallback(
    std::string file_path,
    long long start,
    long long end,
    std::string content_type,
    X::Value callback) {
  const bool ok = StreamFile(std::move(file_path), start, end, std::move(content_type));
  if (ok) {
    X::Value ignored;
    std::vector<X::Value> args;
    args.emplace_back(static_cast<long long>(response_ == nullptr ? 0 : response_->body.size()));
    args.emplace_back(static_cast<long long>(response_ == nullptr ? 0 : response_->body.size()));
    callback.Call(args, ignored);
  }
  return ok;
}

HttpClient::HttpClient(std::string url) : base_url_(std::move(url)) {
  client_ = std::make_unique<httplib::Client>(base_url_);
}

bool HttpClient::Get(std::string path) {
  if (!client_) return false;
  auto result = client_->Get(path, dict_to_headers(headers()));
  return CaptureResult(result);
}

bool HttpClient::Post(std::string path, std::string content_type, std::string body) {
  if (!client_) return false;
  auto result = client_->Post(path, dict_to_headers(headers()), body, content_type);
  return CaptureResult(result);
}

bool HttpClient::PostWithCallback(std::string path, std::string content_type, std::string body, X::Value callback) {
  const bool ok = Post(std::move(path), std::move(content_type), std::move(body));
  if (ok) {
    X::Value ignored;
    std::vector<X::Value> args;
    args.emplace_back(body_);
    callback.Call(args, ignored);
  }
  return ok;
}

void HttpClient::SetHeaders(X::Value headers) {
  if (headers.IsDict()) {
    headers_ = std::move(headers);
    return;
  }
  auto dict = X::Value::Dict(Host());
  std::istringstream stream(headers.ToString(false));
  std::string part;
  while (std::getline(stream, part, ';')) {
    const auto colon = part.find(':');
    if (colon == std::string::npos) continue;
    auto key = part.substr(0, colon);
    auto value = part.substr(colon + 1);
    key.erase(key.begin(), std::find_if(key.begin(), key.end(), [](unsigned char c) { return !std::isspace(c); }));
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char c) { return !std::isspace(c); }));
    if (!key.empty()) dict.Set(key.c_str(), X::Value::String(Host(), value));
  }
  headers_ = std::move(dict);
}

X::Value HttpClient::Headers() const { return headers_; }
bool HttpClient::EnableServerCertificateVerification() const { return verify_server_cert_; }
long long HttpClient::Status() const { return status_; }
X::Value HttpClient::ResponseHeaders() const { return response_headers_; }
X::Value HttpClient::Body() const { return body_; }

X::Value HttpClient::headers() {
  if (!headers_.IsValid()) {
    headers_ = X::Value::Dict(Host());
  }
  return headers_;
}

bool HttpClient::CaptureResult(const httplib::Result& result) {
  if (!result) {
    status_ = 0;
    body_ = X::Value::String(Host(), "");
    response_headers_ = X::Value::Dict(Host());
    return false;
  }
  status_ = result->status;
  response_headers_ = headers_to_dict(Host(), result->headers);
  body_ = is_text_type(result->headers)
      ? X::Value::String(Host(), result->body)
      : X::Value::Bytes(Host(), result->body.data(), result->body.size());
  return true;
}

HttpServer::HttpServer(xlang_net_http* owner)
    : request_class_(owner->__xlang3_package_->GetValue("Request")),
      response_class_(owner->__xlang3_package_->GetValue("Response")) {
  Init();
}

HttpServer::~HttpServer() {
  Stop();
}

bool HttpServer::Listen(std::string address, int port, int backlog, int thread_pool_count) {
  if (!server_) Init();
  server_->set_listen_backlog(backlog);
  if (thread_pool_count > 0) {
    server_->set_thread_pool_count(thread_pool_count);
  }
  return server_->listen(address, port);
}

bool HttpServer::Stop() {
  if (server_) {
    server_->stop();
  }
  return true;
}

bool HttpServer::Get(std::string pattern, X::Value handler) {
  if (!server_) Init();
  server_->Get(pattern, [this, handler](const httplib::Request& req, httplib::Response& res) mutable {
    if (!CallHandler(handler, req, res, {})) {
      res.status = 500;
      if (res.body.empty()) {
        res.set_content("xlang_net.http handler failed", "text/plain");
      }
    }
  });
  return true;
}

bool HttpServer::AddRoute(std::string pattern, X::Value handler) {
  routes_.push_back({pattern, std::regex(translate_route_pattern(pattern)), handler});
  return true;
}

bool HttpServer::Route(X::Value handler) {
  return AddRoute("/<path>", std::move(handler));
}

X::Value HttpServer::GetRoutes() const {
  auto list = X::Value::List(Host());
  for (const auto& route : routes_) {
    auto item = X::Value::Dict(Host());
    item.Set("rule", X::Value::String(Host(), route.rule));
    list.Append(item);
  }
  return list;
}

bool HttpServer::SetAuthenticationCallback(X::Value callback, X::Value parameters) {
  auth_callback_ = std::move(callback);
  auth_parameters_ = std::move(parameters);
  return true;
}

X::Value HttpServer::GetMimeType(std::string extension) const {
  auto type = mime_type_and_binary(extension);
  auto list = X::Value::List(Host());
  list.Append(X::Value::String(Host(), type.first));
  list.Append(X::Value(type.second));
  return list;
}

bool HttpServer::SupportStaticFiles() const { return support_static_files_; }
X::Value HttpServer::StaticIndexFile() const { return X::Value::String(Host(), static_index_file_); }

X::Value HttpServer::StaticRoots() const {
  auto list = X::Value::List(Host());
  for (const auto& root : static_roots_) {
    list.Append(X::Value::String(Host(), root));
  }
  return list;
}

void HttpServer::Init() {
  server_ = std::make_unique<httplib::Server>();
  server_->new_task_queue = []() { return new InlineTaskQueue(); };
  server_->set_routing_handler([this](const httplib::Request& req, httplib::Response& res) {
    for (auto& route : routes_) {
      std::smatch matches;
      if (!std::regex_search(req.path, matches, route.regex)) {
        continue;
      }
      std::vector<X::Value> captures;
      for (size_t i = 1; i < matches.size(); ++i) {
        captures.push_back(X::Value::String(Host(), matches[i].str()));
      }
      return CallHandler(route.handler, req, res, captures)
          ? httplib::Server::HandlerResponse::Handled
          : httplib::Server::HandlerResponse::Unhandled;
    }
    return httplib::Server::HandlerResponse::Unhandled;
  });
}

bool HttpServer::CallHandler(X::Value handler, const httplib::Request& req, httplib::Response& res, std::vector<X::Value> args) {
  X::Value req_value = create_native_instance(Host(), request_class_, new HttpRequest(&req));
  X::Value res_value = create_native_instance(Host(), response_class_, new HttpResponse(&res));
  if (!req_value.IsValid() || !res_value.IsValid()) return false;
  args.push_back(req_value);
  args.push_back(res_value);
  X::Value ret;
  if (!handler.Call(args, ret)) {
    const char* error = Host() != nullptr && Host()->runtime_last_error != nullptr ? Host()->runtime_last_error(Host()->runtime) : nullptr;
    if (error != nullptr && error[0] != '\0') {
      res.set_content(std::string("xlang_net.http handler failed: ") + error, "text/plain");
    }
    return false;
  }
  if (ret.IsValid() && ret.raw().tag != X3_TAG_NONE) {
    HttpResponse wrapper(&res);
    wrapper.__xlang3_host_ = Host();
    wrapper.SetContent(ret, "text/html");
  }
  return true;
}

X::Value xlang_net_http::WritePad(X::Value input) {
  return input;
}

} // namespace xlang_net
