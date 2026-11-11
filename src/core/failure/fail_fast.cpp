#include "fail_fast.hpp"
#include "failure_snapshot.hpp"

#include <algorithm>
#include <cstddef>
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
#if __has_include(<dlfcn.h>)
#include <dlfcn.h>
#define GUI_FORMS_FAIL_FAST_DLADDR 1
#endif
#endif

namespace gui_forms::detail {
namespace {

// At most this many frames are printed; the report must stay readable.
constexpr int maximum_stack_frames = 48;
// At most this many characters of a site are printed (it fits an int).
constexpr std::size_t maximum_site_characters = 256U;

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

// The snapshot's shadow frames, outermost first, one line each: the kind's
// number, the C ABI function for a library call, the subject, the name and
// the sequence. Frame texts are at most 48 and 32 bytes, so they fit an int
// precision.
void write_shadow_frames(const FailureSnapshot& snapshot) noexcept {
    std::fprintf(stderr, "GUI.Forms: shadow frames (%u open, %u recorded):\n", static_cast<unsigned>(snapshot.depth),
                 static_cast<unsigned>(snapshot.frame_count));
    for (std::uint32_t index = 0U; index < snapshot.frame_count; ++index) {
        const ShadowFrame& frame = snapshot.frames[index];
        std::fprintf(stderr, "  #%u kind %u", static_cast<unsigned>(index), static_cast<unsigned>(frame.kind));
        if (frame.kind == ShadowFrameKind::library_call) {
            std::fprintf(stderr, " table %u slot %u", static_cast<unsigned>(frame.interface_id),
                         static_cast<unsigned>(frame.function_index));
        }
        std::fprintf(stderr, " %.*s %.*s %llu\n", static_cast<int>(frame.subject_size), frame.subject.data(),
                     static_cast<int>(frame.name_size), frame.name.data(),
                     static_cast<unsigned long long>(frame.sequence));
    }
}

} // namespace

void write_fail_fast_stack() noexcept {
    write_stack();
}

void write_code_address(const void* address) noexcept {
    if (address == nullptr) {
        std::fputs("unknown", stderr);
        return;
    }
#if defined(_WIN32)
    HMODULE module = nullptr;
    char path[MAX_PATH]{};
    const DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
    const bool found = GetModuleHandleExA(flags, static_cast<LPCSTR>(address), &module) != 0 &&
        GetModuleFileNameA(module, path, MAX_PATH) != 0U;
    if (found) {
        const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(module);
        const std::uintptr_t offset = reinterpret_cast<std::uintptr_t>(address) - base;
        std::fprintf(stderr, "%s+0x%llx", path, static_cast<unsigned long long>(offset));
        return;
    }
#elif defined(GUI_FORMS_FAIL_FAST_DLADDR)
    Dl_info information{};
    if (dladdr(address, &information) != 0 && information.dli_fname != nullptr) {
        const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(information.dli_fbase);
        const std::uintptr_t offset = reinterpret_cast<std::uintptr_t>(address) - base;
        const char* const symbol = information.dli_sname != nullptr ? information.dli_sname : "?";
        std::fprintf(stderr, "%s+0x%llx (%s)", information.dli_fname, static_cast<unsigned long long>(offset),
                     symbol);
        return;
    }
#endif
    std::fprintf(stderr, "%p", address);
}

void fail_fast(std::string_view site, std::exception_ptr failure) noexcept {
    // The snapshot first, while the unwind mark is as the boundary's catch
    // left it; a debugger or a dump finds it in the thread's storage too.
    const FailureBoundary boundary{site, failure_native_exception_status, 0U, 0U};
    record_failure_snapshot(boundary, failure);
    // %.*s takes an int precision; a site is a short literal, and a longer
    // one is cut at maximum_site_characters.
    const std::size_t shown = std::min(site.size(), maximum_site_characters);
    std::fprintf(stderr, "GUI.Forms: fatal exception at %.*s: ", static_cast<int>(shown), site.data());
    write_exception_text(failure);
    std::fputs("\n", stderr);
    write_shadow_frames(thread_failure_snapshot());
    std::fputs("GUI.Forms: stack:\n", stderr);
    write_stack();
    std::fflush(stderr);
    std::abort();
}

} // namespace gui_forms::detail
