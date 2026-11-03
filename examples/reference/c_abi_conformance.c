/*
 * The C ABI conformance kit as a product runs it: a C11 program built
 * against the installed SDK's CAbiConformance component runs the kit on its
 * main thread and fails when any check fails. The window checks need a
 * display, so a headless run passes --no-window (docs/c-abi/conformance-kit.md).
 */
#include <gui_forms/c/gf_conformance.h>

#include <stdio.h>
#include <string.h>

int main(int argc, char** argv) {
    gf_conformance_flags flags = GF_CONFORMANCE_FLAGS_WINDOW;
    int index = 1;
    for (index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--no-window") == 0) {
            flags = GF_CONFORMANCE_FLAGS_NONE;
        }
    }
    const uint32_t failures = gf_conformance_run(stdout, flags);
    return failures == 0u ? 0 : 1;
}
