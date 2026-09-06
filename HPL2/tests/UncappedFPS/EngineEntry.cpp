#include <string>

// The engine library also supplies its Windows application entry point. These
// console tests use main(), but the unused entry point still needs this symbol.
int hplMain(const std::string&) { return 0; }
