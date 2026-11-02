#ifndef GUI_FORMS_TESTS_ABI_GF_CONFORMANCE_H
#define GUI_FORMS_TESTS_ABI_GF_CONFORMANCE_H

/*
 * The GUI.Forms C ABI conformance kit, part 1: ownership, threads,
 * callbacks and the window run. A consumer links the installed CAbi and runs
 * the kit in its own tests (owner decision C-3); the library's own tests run
 * it in-tree. The kit uses only gf_abi.h and pthreads.
 */

#include "gui_forms/c/gf_abi.h"

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* gf_conformance_flags: which parts run. */
typedef uint32_t gf_conformance_flags;
#define GF_CONFORMANCE_FLAGS_NONE 0u
/* Runs the window checks, which show a window on the host briefly. */
#define GF_CONFORMANCE_FLAGS_WINDOW 1u

/*
 * Runs the kit on the calling thread, which becomes the library's UI thread
 * (no object may exist when it starts). Writes one line per failed check and
 * a summary to out (null for none). Returns the number of failed checks.
 */
uint32_t gf_conformance_run(FILE* out, gf_conformance_flags flags);

#ifdef __cplusplus
}
#endif

#endif
