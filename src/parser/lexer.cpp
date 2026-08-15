#include "xlang3/parser.h"

#include <cctype>
#include <sstream>

namespace xlang3 {

Lexer::Lexer(std::string source) : source_(std::move(source)) {}

void Lexer::emit(TokenKind kind, std::string text, uint32_t line, uint32_t column) {
  tokens_.push_back(Token{kind, std::move(text), line, column});
}

std::vector<Token> Lexer::tokenize() {
  std::istringstream input(source_);
  std::string line;
  uint32_t line_no = 1;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    uint32_t indent = 0;
    while (indent < line.size() && line[indent] == ' ') {
      ++indent;
    }
    const auto first = line.find_first_not_of(" \t");
    if (first == std::string::npos || line[first] == '#') {
      ++line_no;
      continue;
    }
    if (indent > indent_stack_.back()) {
      indent_stack_.push_back(indent);
      emit(TokenKind::Indent, "", line_no, 1);
    } else {
      while (indent < indent_stack_.back()) {
        indent_stack_.pop_back();
        emit(TokenKind::Dedent, "", line_no, 1);
      }
      if (indent != indent_stack_.back()) {
        errors_.push_back("line " + std::to_string(line_no) + ": inconsistent indentation");
      }
    }
    tokenize_line(line, line_no, indent);
    emit(TokenKind::Newline, "", line_no, static_cast<uint32_t>(line.size() + 1));
    ++line_no;
  }
  while (indent_stack_.size() > 1) {
    indent_stack_.pop_back();
    emit(TokenKind::Dedent, "", line_no, 1);
  }
  emit(TokenKind::End, "", line_no, 1);
  return tokens_;
}

void Lexer::tokenize_line(const std::string& line_text, uint32_t line_no, uint32_t indent) {
  size_t i = indent;
  while (i < line_text.size()) {
    const char ch = line_text[i];
    const uint32_t col = static_cast<uint32_t>(i + 1);
    if (ch == ' ' || ch == '\t') {
      ++i;
      continue;
    }
    if (ch == '#') {
      return;
    }
    if (std::isalpha(static_cast<unsigned char>(ch)) || ch == '_') {
      size_t start = i++;
      while (i < line_text.size() &&
             (std::isalnum(static_cast<unsigned char>(line_text[i])) || line_text[i] == '_')) {
        ++i;
      }
      std::string text = line_text.substr(start, i - start);
      TokenKind kind = TokenKind::Identifier;
      if (text == "def") kind = TokenKind::KwDef;
      else if (text == "return") kind = TokenKind::KwReturn;
      else if (text == "if") kind = TokenKind::KwIf;
      else if (text == "else") kind = TokenKind::KwElse;
      else if (text == "while") kind = TokenKind::KwWhile;
      else if (text == "nonlocal") kind = TokenKind::KwNonlocal;
      else if (text == "True") kind = TokenKind::KwTrue;
      else if (text == "False") kind = TokenKind::KwFalse;
      else if (text == "None") kind = TokenKind::KwNone;
      else if (text == "and") kind = TokenKind::KwAnd;
      else if (text == "or") kind = TokenKind::KwOr;
      else if (text == "not") kind = TokenKind::KwNot;
      emit(kind, text, line_no, col);
      continue;
    }
    if (std::isdigit(static_cast<unsigned char>(ch))) {
      size_t start = i++;
      bool is_double = false;
      while (i < line_text.size() && std::isdigit(static_cast<unsigned char>(line_text[i]))) {
        ++i;
      }
      if (i < line_text.size() && line_text[i] == '.') {
        is_double = true;
        ++i;
        while (i < line_text.size() && std::isdigit(static_cast<unsigned char>(line_text[i]))) {
          ++i;
        }
      }
      emit(is_double ? TokenKind::Double : TokenKind::Integer, line_text.substr(start, i - start), line_no, col);
      continue;
    }
    if (ch == '"' || ch == '\'') {
      const char quote = ch;
      size_t start = ++i;
      std::string value;
      bool closed = false;
      while (i < line_text.size()) {
        if (line_text[i] == quote) {
          closed = true;
          break;
        }
        if (line_text[i] == '\\' && i + 1 < line_text.size()) {
          ++i;
          if (line_text[i] == 'n') value.push_back('\n');
          else value.push_back(line_text[i]);
          ++i;
          continue;
        }
        value.push_back(line_text[i++]);
      }
      if (!closed) {
        errors_.push_back("line " + std::to_string(line_no) + ": unterminated string");
        return;
      }
      ++i;
      (void)start;
      emit(TokenKind::String, value, line_no, col);
      continue;
    }
    auto two = i + 1 < line_text.size() ? line_text.substr(i, 2) : std::string{};
    if (two == "==") { emit(TokenKind::EqualEqual, two, line_no, col); i += 2; continue; }
    if (two == "!=") { emit(TokenKind::NotEqual, two, line_no, col); i += 2; continue; }
    if (two == "<=") { emit(TokenKind::LessEqual, two, line_no, col); i += 2; continue; }
    if (two == ">=") { emit(TokenKind::GreaterEqual, two, line_no, col); i += 2; continue; }
    switch (ch) {
      case '(': emit(TokenKind::LParen, "(", line_no, col); break;
      case ')': emit(TokenKind::RParen, ")", line_no, col); break;
      case ',': emit(TokenKind::Comma, ",", line_no, col); break;
      case ':': emit(TokenKind::Colon, ":", line_no, col); break;
      case '=': emit(TokenKind::Assign, "=", line_no, col); break;
      case '+': emit(TokenKind::Plus, "+", line_no, col); break;
      case '-': emit(TokenKind::Minus, "-", line_no, col); break;
      case '*': emit(TokenKind::Star, "*", line_no, col); break;
      case '/': emit(TokenKind::Slash, "/", line_no, col); break;
      case '%': emit(TokenKind::Percent, "%", line_no, col); break;
      case '<': emit(TokenKind::Less, "<", line_no, col); break;
      case '>': emit(TokenKind::Greater, ">", line_no, col); break;
      default:
        errors_.push_back("line " + std::to_string(line_no) + ": unexpected character '" + ch + "'");
        break;
    }
    ++i;
  }
}

} // namespace xlang3
