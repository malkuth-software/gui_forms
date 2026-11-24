// Proves that gui_forms_use_library_allocator binds: built twice
// (CMakeLists.txt), as gui_forms_allocator_probe, which opts in, and as
// gui_forms_allocator_probe_control, which does not. tools/ci_build.py runs
// both on every desktop host, the sanitizer builds included.
//
// The opted-in program asserts that the replacement reported itself live
// (gui_forms::detail::program_allocator) and that the blocks operator new,
// operator new[], the aligned form and std::allocator hand out are STXlib's:
// it gives each to stxlib::memory::release, which ends the process with a
// report when given a block that is not its own (stx_minmallocpp in every
// build, the host backend with its tags). Under a sanitizer it asserts the
// opposite: the replacement left itself out, so the sanitizer sees every
// allocation.
//
// The control asserts that nothing reported a replacement, and that
// operator new's blocks are the C library's: the toolchain's operator new
// (libc++'s, or STXlib's Windows runtime) takes them from malloc, so free
// accepts one. Under a sanitizer that free is skipped (a mismatched
// deallocation it would report).
//
// Exit status 0 on success; 1 with a message on stderr otherwise.

#include "gui_forms/allocation/library_memory/library_memory.hpp"

#include <stxlib/memory.hpp>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <vector>

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__) || defined(__SANITIZE_HWADDRESS__)
#define GUI_FORMS_PROBE_SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer) || __has_feature(memory_sanitizer) || \
    __has_feature(hwaddress_sanitizer)
#define GUI_FORMS_PROBE_SANITIZED 1
#else
#define GUI_FORMS_PROBE_SANITIZED 0
#endif
#else
#define GUI_FORMS_PROBE_SANITIZED 0
#endif

namespace {

using gui_forms::detail::ProgramAllocator;

const char* state_name(const ProgramAllocator state) {
    switch (state) {
    case ProgramAllocator::not_linked:
        return "not_linked";
    case ProgramAllocator::left_out:
        return "left_out";
    case ProgramAllocator::replaced:
        return "replaced";
    }
    return "unknown";
}

int expect_state(const ProgramAllocator expected) {
    const ProgramAllocator state = gui_forms::detail::program_allocator();
    if (state != expected) {
        std::fprintf(stderr, "allocator probe: the program allocator is %s, expected %s\n", state_name(state),
                     state_name(expected));
        return 1;
    }
    return 0;
}

#if GUI_FORMS_PROBE_OPTED_IN && !GUI_FORMS_PROBE_SANITIZED
// Each block goes back through STXlib's release, which refuses (ends the
// process) a block that is not its own.
int expect_library_blocks() {
    if (!stxlib::memory::tags_checked()) {
        std::fprintf(stderr, "allocator probe: this STXlib build does not check releases, so the probe proves "
                             "nothing\n");
        return 1;
    }
    stxlib::memory::release(::operator new(48));
    stxlib::memory::release(::operator new[](48));
    stxlib::memory::release(::operator new(256, std::align_val_t{64}));
    std::allocator<int> integers;
    stxlib::memory::release(integers.allocate(1000));
    // A container's own round trip, through the replaced delete.
    std::vector<int> values;
    for (int index = 0; index < 4096; ++index) values.push_back(index);
    if (values.back() != 4095) return 1;
    return 0;
}
#endif

} // namespace

int main() {
#if GUI_FORMS_PROBE_OPTED_IN
#if GUI_FORMS_PROBE_SANITIZED
    if (expect_state(ProgramAllocator::left_out) != 0) return 1;
    std::puts("allocator probe: opted in, left out under the sanitizer");
#else
    if (expect_state(ProgramAllocator::replaced) != 0) return 1;
    if (expect_library_blocks() != 0) return 1;
    std::puts("allocator probe: opted in, operator new is STXlib's allocator");
#endif
#else
    if (expect_state(ProgramAllocator::not_linked) != 0) return 1;
#if !GUI_FORMS_PROBE_SANITIZED
    std::free(::operator new(48));
#endif
    std::puts("allocator probe: not opted in, operator new is the toolchain's");
#endif
    return 0;
}
