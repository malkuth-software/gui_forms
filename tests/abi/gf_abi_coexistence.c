/*
 * Compiled into gui_forms_abi_layout_probe_c11: ABI 1's header and the
 * experimental 0.x header declare no name twice, so a program that is moving
 * from one to the other can include both in one translation unit.
 */
#include "gui_forms/c/gf_abi.h"
#include "gui_forms/c_api.h"

/* One use of each header, so neither include is dead. */
const uint32_t gf_abi_coexistence_versions[2] = {GF_ABI_MAJOR, GF_ABI_VERSION_0_26};
