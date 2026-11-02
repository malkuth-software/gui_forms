/* Runs the conformance kit against the library this program links. With
 * --no-window it skips the window checks (a host without a display). */

#include "gf_conformance.h"

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
