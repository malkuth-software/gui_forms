// The consumer-macro shield restores the consumer's macros exactly
// (tools/consumer_shield.py). Macros are defined before GUI.Forms headers
// are included in nested, repeated and mixed orders, and are checked after.
// Compiled, never run: a failed check is a compile error.
#define near 1
#define max(a, b) 7
#define Button 3
#define consumer_only_name 5

// A header that opens the shield itself, alone.
#include "gui_forms/c/gf_abi.h"
#if near != 1 || max(1, 2) != 7 || Button != 3 || consumer_only_name != 5
#error "the C ABI header did not restore the consumer's macros"
#endif

// The consumer's own shield around C++ headers, with a self-opening header
// nested inside it and one included again (skipped by its guard).
#include "gui_forms/gf_shield_begin.h"
#include "gui_forms/controls/label_values.hpp"
#include "gui_forms/c/gf_abi.hpp"
#include "gui_forms/c/gf_abi.h"
#include "gui_forms/controls/button.hpp"
#include "gui_forms/gf_shield_end.h"
#if near != 1 || max(1, 2) != 7 || Button != 3 || consumer_only_name != 5
#error "the consumer's shield did not restore the consumer's macros"
#endif
#ifdef GF_SHIELD_ACTIVE
#error "the shield is still open"
#endif

// A second shield in the same unit, after the first closed.
#include "gui_forms/gf_shield_begin.h"
#include "gui_forms/basic_controls.hpp"
#include "gui_forms/gf_shield_end.h"
#if near != 1 || max(1, 2) != 7 || Button != 3 || consumer_only_name != 5
#error "the second shield did not restore the consumer's macros"
#endif

// A self-opening header after the shields: it opens and closes its own.
#include "gui_forms/drawing_c_api.h"
#if near != 1 || max(1, 2) != 7 || Button != 3 || consumer_only_name != 5
#error "drawing_c_api.h did not restore the consumer's macros"
#endif
