// Hostile-macro conformance, minimal program, header path: the headers a
// program that opens one window of basic controls includes, wrapped in the
// consumer-macro shield as a consumer without modules wraps them.
// hostile_macros_minimal.h is generated at build time
// (tools/generate_hostile_macros.py) from the includes below it: it includes
// the standard headers they reach, then defines every identifier of their
// closure that a consumer may define as a macro that fails the compile where
// it expands. This file compiles only if no consumer macro, known or not, can
// reach the consumer surface through the shield.
#include "hostile_macros_minimal.h"

#include "gui_forms/gf_shield_begin.h"
#include "gui_forms/application.hpp"
#include "gui_forms/window.hpp"
#include "gui_forms/basic_controls.hpp"
#include "gui_forms/gf_shield_end.h"
