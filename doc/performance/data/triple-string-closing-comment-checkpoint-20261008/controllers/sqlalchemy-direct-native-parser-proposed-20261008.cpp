// Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
// Licensed under the Apache License, Version 2.0.
// Diagnostic only: parse one unchanged file through the current DLL; never execute it.
#define NOMINMAX
#include <Windows.h>
#include "xlang3/parser.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

static void json_string(std::ostream& out, std::string_view value) {
  static const char hex[] = "0123456789abcdef";
  out << '"';
  for (unsigned char c : value) {
    if (c == '"' || c == '\\') out << '\\' << static_cast<char>(c);
    else if (c < 0x20) out << "\\u00" << hex[c >> 4] << hex[c & 15];
    else out << static_cast<char>(c);
  }
  out << '"';
}

static std::string runtime_dll_path() {
  HMODULE module = GetModuleHandleW(L"xlang3_runtime.dll");
  if (!module) throw std::runtime_error("current runtime DLL was not loaded");
  std::wstring path(32768, L'\0');
  DWORD count = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
  if (!count || count >= path.size()) throw std::runtime_error("runtime DLL path unavailable or truncated");
  int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path.data(), static_cast<int>(count), nullptr, 0, nullptr, nullptr);
  if (!bytes) throw std::runtime_error("runtime DLL path UTF-8 conversion failed");
  std::string utf8(static_cast<size_t>(bytes), '\0');
  if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path.data(), static_cast<int>(count), utf8.data(), bytes, nullptr, nullptr) != bytes)
    throw std::runtime_error("runtime DLL path UTF-8 conversion failed");
  return utf8;
}

static void errors_json(const std::vector<std::string>& errors) {
  std::cout << '[';
  for (size_t i = 0; i < errors.size(); ++i) {
    if (i) std::cout << ',';
    json_string(std::cout, errors[i]);
  }
  std::cout << ']';
}

int main(int argc, char** argv) {
  std::string dll;
  try {
    if (argc != 2) throw std::runtime_error("usage: direct-parser.exe one-input.py");
    dll = runtime_dll_path();
    std::ifstream input(argv[1], std::ios::binary);
    if (!input) throw std::runtime_error("cannot open input file");
    // Keep this exact byte owner alive until both parser and returned AST are retired.
    const std::string source{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (input.bad()) throw std::runtime_error("input read failed");
    if (source.size() > 2 * 1024 * 1024) throw std::runtime_error("input exceeds diagnostic 2 MiB cap");
    xlang3::Lexer lexer(source);
    auto lexed = lexer.tokenize();
    const auto lexical_errors = lexed.errors;
    xlang3::Parser parser(std::move(lexed), source);
    auto parsed = parser.parse_module();
    std::cout << "{\"mode\":\"direct_native_parser_diagnostic_only\",\"input\":";
    json_string(std::cout, argv[1]);
    std::cout << ",\"loaded_runtime_dll\":";
    json_string(std::cout, dll);
    std::cout << ",\"input_bytes\":" << source.size()
              << ",\"lexer_error_count\":" << lexical_errors.size()
              << ",\"parser_error_count\":" << parsed.errors.size()
              << ",\"module_statement_count\":" << parsed.module.body.size()
              << ",\"lexer_errors\":";
    errors_json(lexical_errors);
    std::cout << ",\"parser_errors\":";
    errors_json(parsed.errors);
    std::cout << ",\"parse_source_wrapper_used\":false,\"imports_executed\":false,\"python_executed\":false}\n";
    return parsed.errors.empty() ? 0 : 1;
  } catch (const std::exception& error) {
    std::cout << "{\"mode\":\"direct_native_parser_diagnostic_only\",\"failure\":";
    json_string(std::cout, error.what());
    std::cout << ",\"loaded_runtime_dll\":";
    json_string(std::cout, dll);
    std::cout << ",\"imports_executed\":false,\"python_executed\":false}\n";
    return 2;
  }
}
