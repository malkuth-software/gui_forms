// The shared precompiled header of the test programs (cmake/TestPrecompiledHeader.cmake).
//
// Each test translation unit reads about 1260 headers, most of them the same
// standard library and toolkit headers; compiling them once per group of
// test programs, instead of once per test source, is most of the test build's
// time. This header names the headers most test sources include. It is
// force-included before each test source of a group, so it must stay free of
// macros a test could depend on defining first (a test that defines one
// before its includes is left out of the groups), and it adds declarations
// only: no test may rely on it for an include of its own, which the
// scheduled build checks by compiling every test without it.
#ifndef GUI_FORMS_TESTS_SUPPORT_TEST_PRECOMPILED_HPP
#define GUI_FORMS_TESTS_SUPPORT_TEST_PRECOMPILED_HPP

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The toolkit's umbrella header, for the groups whose programs see the
// toolkit's include directory (every program linking a toolkit library).
#if __has_include("gui_forms/gui_forms.hpp")
#include "gui_forms/gui_forms.hpp"
#endif

#endif
