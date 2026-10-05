#pragma once
// Isolates sol2's harmless but noisy warnings (false-positive array-bounds in
// its get_or machinery) from the rest of the build.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
#pragma GCC diagnostic ignored "-Wstringop-overread"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

#include <sol/sol.hpp>

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
