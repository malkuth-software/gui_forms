// Hostile-macro conformance, the platform entry headers, header path: a
// program includes the one for its host. All are portable headers (no
// platform type appears in them), so every host compiles all of them; see
// hostile_macro_minimal.cpp for the method.
#include "hostile_macros_platform_entries.h"

#include "gui_forms/gf_shield_begin.h"
#include "gui_forms/platform/linux_host.hpp"
#include "gui_forms/platform/macos_host.hpp"
#include "gui_forms/platform/surface_host.hpp"
#include "gui_forms/platform/web_host.hpp"
#include "gui_forms/platform/windows_host.hpp"
#include "gui_forms/gf_shield_end.h"
