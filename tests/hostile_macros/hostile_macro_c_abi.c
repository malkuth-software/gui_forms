/* Hostile-macro conformance, the C ABI compiled as C (see
   hostile_macro_minimal.cpp for the method). Its names carry the reserved
   prefixes; gf_abi.h and drawing_c_api.h open the consumer-macro shield
   themselves, so their record fields and parameters survive the rest. */
#include "hostile_macros_c_abi.h"

#include "gui_forms/c/gf_abi.h"
#include "gui_forms/drawing_c_api.h"
