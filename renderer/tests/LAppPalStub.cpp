// Test-only stub for the LAppPal symbols referenced by AudioManager.cpp.
// The real LAppPal.cpp lives in the main executable and drags in the Cubism
// Framework runtime; the tests only need the logging sink.
#include "LAppPal.hpp"

#include <cstdarg>
#include <cstdio>

void LAppPal::PrintLogLn(const Csm::csmChar* format, ...)
{
    va_list args;
    va_start(args, format);
    std::vfprintf(stderr, format, args);
    va_end(args);
    std::fprintf(stderr, "\n");
}
