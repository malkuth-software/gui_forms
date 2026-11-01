#pragma once

#include "gui_forms/gui_forms.hpp"

#include <string_view>

// The other translation units of the localizable-literal plugin test
// (tests/localizable_plugin_tests.cpp).
namespace localizable_units {

// Compiled without the plugin: its literal is not recorded.
void plain_unit(gui_forms::Label& label);
// Compiled with the plugin into a static library.
void static_unit(gui_forms::Label& label);
// Compiled with the plugin into a shared library, which has its own table.
#if defined(_WIN32)
__declspec(dllimport)
#else
__attribute__((visibility("default")))
#endif
std::string_view shared_unit();

} // namespace localizable_units
