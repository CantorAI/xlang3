// Standalone ownership probe: link this against a preserved runtime import
// library to check the control DLL without rebuilding or swapping the engine.
#include "../../tests/cpp/mapping_iterator_ownership_cases.h"
#include <string_view>

int main(int argc, char** argv) {
  const bool control = argc == 2 && std::string_view(argv[1]) == "--control";
  if (argc > 1 && !control) return 2;
  xlang3::test::CaseResult result;
  xlang3::test::check_mapping_iterator_ownership(result, !control);
  return xlang3::test::finish(result);
}
