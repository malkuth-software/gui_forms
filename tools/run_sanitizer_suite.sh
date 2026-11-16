#!/bin/sh
# Build the toolkit with AddressSanitizer and UndefinedBehaviorSanitizer and
# run the whole ctest suite. A local procedure, not a CI job.
#
#   sh tools/run_sanitizer_suite.sh <build-directory> [extra cmake arguments]
#
# Run it from the repository root in the toolchain shell that builds the
# project normally (on Windows the MSYS2 CLANG64 shell with LLVM 22.1.8;
# docs/building/index.md has the details). The first run configures the
# build directory; later runs rebuild incrementally. Extra arguments are
# passed to the first configure only. The defaults are the Windows native
# configuration; on Linux or macOS pass the host and Skia options that a
# normal build of that platform uses.
#
# The suite runs with ASan halting at its first report and UBSan reporting
# every finding without halting. A UBSan report therefore does not fail its
# test, so the script reads every test's output from ctest's LastTest.log
# and lists the reports itself. It exits non-zero when a test fails or when
# any sanitizer report appears. Rerun a test that fails only under parallel
# load on its own (ctest -R <name>) before treating it as a finding.
set -eu

if test "$#" -lt 1; then
    echo "usage: sh tools/run_sanitizer_suite.sh <build-directory> [cmake arguments]" >&2
    exit 2
fi
build_directory=$1
shift
source_directory=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if command -v cygpath >/dev/null 2>&1; then
    # Native Windows CMake needs a Windows path for the toolchain file.
    source_directory=$(cygpath -m "$source_directory")
fi
sanitizers="-fsanitize=address,undefined -fno-omit-frame-pointer"
jobs=${GUI_FORMS_SANITIZER_JOBS:-3}

if test ! -f "$build_directory/build.ninja"; then
    cmake -S "$source_directory" -B "$build_directory" -G Ninja \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_LIBDIR=lib \
        -DCMAKE_TOOLCHAIN_FILE="$source_directory/cmake/llvm22.cmake" \
        -DCMAKE_C_FLAGS="$sanitizers" -DCMAKE_CXX_FLAGS="$sanitizers" \
        -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" \
        -DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=address,undefined" \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DGUI_FORMS_BUILD_GALLERY=ON -DGUI_FORMS_BUILD_TESTS=ON \
        -DGUI_FORMS_BUILD_AUDIO=ON -DGUI_FORMS_AUDIO_VORBIS_BACKEND=stx \
        -DGUI_FORMS_ENABLE_WINDOWS_HOST=ON -DGUI_FORMS_ENABLE_MACOS_HOST=OFF \
        -DGUI_FORMS_ENABLE_LINUX_HOST=OFF -DGUI_FORMS_ENABLE_SKIA=OFF "$@"
fi
cmake --build "$build_directory" --parallel "$jobs"

ASAN_OPTIONS="halt_on_error=1:print_summary=1:symbolize=1"
UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=0:print_summary=1"
export ASAN_OPTIONS UBSAN_OPTIONS
ctest_status=0
# Sanitized tests run several times slower than normal ones; tests with their
# own TIMEOUT property keep it.
ctest --test-dir "$build_directory" --timeout 1500 -j "$jobs" --output-on-failure || ctest_status=$?

log="$build_directory/Testing/Temporary/LastTest.log"
reports=$(grep -E "runtime error:|ERROR: AddressSanitizer|SUMMARY: (Address|UndefinedBehavior)Sanitizer" "$log" || true)
if test -n "$reports"; then
    echo "Sanitizer reports (full output in $log):"
    echo "$reports" | sort | uniq -c
fi
if test "$ctest_status" -ne 0 || test -n "$reports"; then
    exit 1
fi
echo "Sanitized suite passed with no sanitizer reports."
