/* A test program whose code lives in a shared runner (cmake/TestRunners.cmake).
 * It runs `GF_TEST_RUNNER GF_TEST_PROGRAM <its arguments>` and returns the
 * runner's exit status: on POSIX it replaces itself with the runner (execv),
 * so ctest sees the runner's status and signals unchanged; on Windows it
 * waits for the runner and returns its exit code.
 *
 * Inputs (compile definitions): GF_TEST_RUNNER, the runner's path;
 * GF_TEST_PROGRAM, this program's name, the runner's first argument. */

#include <stdio.h>
#include <stdlib.h>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

int main(int argument_count, char** arguments) {
    /* The runner, this program's name, its arguments and the terminator. */
    const size_t slots = (size_t)argument_count + 2U;
    char** const forwarded = (char**)calloc(slots, sizeof(char*));
    if (forwarded == NULL) {
        fputs("test launcher: out of memory\n", stderr);
        return 2;
    }
    forwarded[0] = (char*)GF_TEST_RUNNER;
    forwarded[1] = (char*)GF_TEST_PROGRAM;
    for (int index = 1; index < argument_count; ++index) {
        forwarded[index + 1] = arguments[index];
    }
    forwarded[slots - 1U] = NULL;
#if defined(_WIN32)
    const intptr_t status = _spawnv(_P_WAIT, GF_TEST_RUNNER, (const char* const*)forwarded);
    free(forwarded);
    if (status == -1) {
        perror("test launcher: " GF_TEST_RUNNER);
        return 2;
    }
    return (int)status;
#else
    execv(GF_TEST_RUNNER, forwarded);
    perror("test launcher: " GF_TEST_RUNNER);
    free(forwarded);
    return 2;
#endif
}
