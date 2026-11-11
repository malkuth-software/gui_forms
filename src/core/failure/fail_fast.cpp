#include "fail_fast.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <stdexcept>
#include <string_view>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__has_include)
#if __has_include(<execinfo.h>)
#include <execinfo.h>
#define GUI_FORMS_FAIL_FAST_EXECINFO 1
#endif
#endif

namespace gui_forms::detail {
namespace {

// At most this many frames are printed; the report must stay readable.
constexpr int maximum_stack_frames = 48;

void write_exception_text(std::exception_ptr failure) noexcept {
    if (!failure) {
        std::fputs("no exception", stderr);
        return;
    }
    try {
        std::rethrow_exception(failure);
    } catch (const std::exception& error) {
        std::fputs(error.what(), stderr);
    } catch (...) {
        std::fputs("an exception not derived from std::exception", stderr);
    }
}

// One line per frame: the address, and on Windows the module and the offset
// within it, so a symbolizer can name the function after the fact.
void write_stack() noexcept {
#if defined(_WIN32)
    void* frames[maximum_stack_frames]{};
    const USHORT count = RtlCaptureStackBackTrace(2U, maximum_stack_frames, frames, nullptr);
    for (USHORT index = 0U; index < count; ++index) {
        HMODULE module = nullptr;
        char path[MAX_PATH]{};
        const DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
        const bool found = GetModuleHandleExA(flags, static_cast<LPCSTR>(frames[index]), &module) != 0 &&
            GetModuleFileNameA(module, path, MAX_PATH) != 0U;
        if (found) {
            const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(module);
            const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(frames[index]);
            std::fprintf(stderr, "  #%u %s+0x%llx\n", static_cast<unsigned>(index), path,
                         static_cast<unsigned long long>(address - base));
        } else {
            std::fprintf(stderr, "  #%u %p\n", static_cast<unsigned>(index), frames[index]);
        }
    }
#elif defined(GUI_FORMS_FAIL_FAST_EXECINFO)
    void* frames[maximum_stack_frames]{};
    const int count = backtrace(frames, maximum_stack_frames);
    backtrace_symbols_fd(frames, count, 2);
#else
    std::fputs("  (no stack on this platform)\n", stderr);
#endif
}

} // namespace

void fail_fast(std::string_view site, std::exception_ptr failure) noexcept {
    std::fprintf(stderr, "GUI.Forms: fatal exception at %.*s: ", static_cast<int>(site.size()), site.data());
    write_exception_text(failure);
    std::fputs("\nGUI.Forms: stack:\n", stderr);
    write_stack();
    std::fflush(stderr);
    std::abort();
}

} // namespace gui_forms::detail
