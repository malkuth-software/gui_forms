// The module-level allocator: the global operator new and delete of one
// module (an executable, or the C ABI library), over STXlib's allocator
// (stxlib/memory.hpp: stx_minmallocpp by default).
//
// Where it is compiled (owner, 2026-11-22: code inside GUI.Forms uses our
// allocator; programs that use GUI.Forms keep theirs; programs our own
// projects issue may opt in):
// - the C ABI library (cmake/CAbi.cmake), always. libc++ is linked
//   statically into the library and kept local, and the export list exports
//   only the entry points, so these definitions bind every allocation inside
//   the module (the engine's standard containers, strings and plain news)
//   and nothing outside it: the host process, a C or C# program, keeps its
//   own operator new. No memory crosses the C ABI (handles, caller buffers,
//   contexts the caller releases), so neither side frees the other's;
// - an executable that opts in with gui_forms_use_library_allocator(target)
//   (CMakeLists.txt, and the installed GUIFormsConfig.cmake), never by
//   default: a program that links GUI.Forms statically is one module with
//   one libc++, so the library cannot have an operator new of its own there;
//   the program chooses for both. GUI.Forms' own types still come from
//   STXlib's allocator in any program (LibraryAllocated,
//   make_library_shared: allocation/library_memory).
// Never in a shared library or plugin that shares the system's C++ runtime:
// its definitions would replace operator new for the whole process.
//
// Its definitions are in an object file, so they take precedence over
// libc++'s, which libc++ defines weak ([replacement.functions]).
//
// Left out (operator new stays the toolchain's) under a sanitizer that
// intercepts the host allocator, or when the program defines
// GUI_FORMS_HOST_OPERATOR_NEW. The malloc family is never replaced.
//
// Every form of [new.delete.single] and [new.delete.array] is replaced, the
// sized and aligned deletes included, so that no pair can mix this
// allocator with libc++'s. These allocations do not pass through GUI.Forms'
// library seam (allocation/library_memory): its counts and test observer
// stay those of the library's own routes.
//
// Failure ([new.delete.single]p3): STXlib's allocator never calls a
// new-handler and never throws. This file does both: on a null block it
// calls the installed new-handler and retries; without one the throwing
// forms throw std::bad_alloc (or abort in a program built without
// exceptions) and the nothrow forms return null.
//
// Threads and the process's ends: STXlib's allocator serves any thread at
// any time, before main (its state is constant-initialised), during a
// thread's exit and during static destruction. On Windows, once ExitProcess
// has stopped the other threads, an allocation the calling thread's cached
// blocks cannot serve returns null (stxlib/memory.hpp, "Process end"). The C
// ABI library's static destructors run there, at its DLL_PROCESS_DETACH, so
// the library marks its teardown from its DllMain
// (src/surface/windows/abi/c_abi_module_teardown.cpp) and from then on a
// refused block comes from GUI.Forms' teardown reserve without calling the
// new-handler; one the reserve cannot hold ends the process through
// stxlib::fail_fast with a record, so no std::bad_alloc ever leaves a
// destructor there (allocation/library_memory/library_teardown.hpp, "A
// module's end"). Every delete succeeds then: STXlib's release returns at
// process end, and a reserve block is left where it is. A program that opts
// in runs its static destructors before ExitProcess stops its threads, so
// its teardown is never marked and a refusal there is true exhaustion, as on
// Linux and macOS, where no thread is stopped at exit or dlclose:
// __cxa_finalize runs the module's destructors (a dlclose, or exit's atexit
// handlers) while STXlib's allocator serves as before.
//
// The program links GUIForms::Memory (the teardown reserve is
// gui_forms::detail's; gui_forms_use_library_allocator links it).

#include "gui_forms/allocation/library_memory/library_teardown.hpp"
#include "gui_forms/allocation/library_memory/program_allocator.hpp"

#include <stxlib/memory.hpp>

#include <cstddef>
#include <cstdlib>
#include <new>

#if defined(GUI_FORMS_HOST_OPERATOR_NEW)
#define GUI_FORMS_REPLACE_OPERATOR_NEW 0
#elif defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__) || defined(__SANITIZE_HWADDRESS__)
#define GUI_FORMS_REPLACE_OPERATOR_NEW 0
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer) || __has_feature(memory_sanitizer) || \
    __has_feature(hwaddress_sanitizer)
#define GUI_FORMS_REPLACE_OPERATOR_NEW 0
#else
#define GUI_FORMS_REPLACE_OPERATOR_NEW 1
#endif
#else
#define GUI_FORMS_REPLACE_OPERATOR_NEW 1
#endif

namespace {

// Reports whether this program's operator new is STXlib's
// (gui_forms::detail::program_allocator), so a test or a startup check can
// tell an opted-in program from one that is not, and a sanitizer build's
// left-out replacement from a live one.
bool note_program_allocator() noexcept {
    gui_forms::detail::note_program_allocator(GUI_FORMS_REPLACE_OPERATOR_NEW
                                                  ? gui_forms::detail::ProgramAllocator::replaced
                                                  : gui_forms::detail::ProgramAllocator::left_out);
    return true;
}

[[maybe_unused]] const bool program_allocator_noted = note_program_allocator();

} // namespace

#if GUI_FORMS_REPLACE_OPERATOR_NEW

namespace {

// The alignment operator new promises without an alignment argument.
constexpr std::size_t default_alignment = __STDCPP_DEFAULT_NEW_ALIGNMENT__;

// The alignment a block is asked for: the requested one (a power of two,
// [new.delete.single]p9), never less than the default.
[[nodiscard]] std::size_t block_alignment(const std::align_val_t alignment) noexcept {
    const std::size_t requested = static_cast<std::size_t>(alignment);
    if (requested < default_alignment) {
        return default_alignment;
    }
    return requested;
}

// No new-handler is installed and the block was refused.
[[noreturn]] void report_exhaustion() {
#if defined(__cpp_exceptions)
    throw std::bad_alloc();
#else
    std::abort();
#endif
}

// The throwing forms: retry after each new-handler call until a block comes
// back or no handler is installed. After the module's teardown began a
// refusal is served by the teardown reserve (or fails fast), never thrown.
[[nodiscard]] void* allocate_or_throw(const std::size_t size, const std::size_t alignment) {
    void* block = stxlib::memory::allocate(size, alignment);
    while (block == nullptr) {
        if (gui_forms::detail::library_teardown_begun()) {
            block = gui_forms::detail::teardown_allocate(size, alignment);
            return block;
        }
        const std::new_handler handler = std::get_new_handler();
        if (handler == nullptr) {
            report_exhaustion();
        }
        handler();
        block = stxlib::memory::allocate(size, alignment);
    }
    return block;
}

// The nothrow forms: as the throwing ones, with a null result where those
// throw, a std::bad_alloc from the new-handler included.
[[nodiscard]] void* allocate_or_null(const std::size_t size, const std::size_t alignment) noexcept {
    void* block = stxlib::memory::allocate(size, alignment);
    while (block == nullptr) {
        if (gui_forms::detail::library_teardown_begun()) {
            block = gui_forms::detail::teardown_allocate(size, alignment);
            return block;
        }
        const std::new_handler handler = std::get_new_handler();
        if (handler == nullptr) {
            return nullptr;
        }
#if defined(__cpp_exceptions)
        try {
            handler();
        } catch (const std::bad_alloc&) {
            return nullptr;
        }
#else
        handler();
#endif
        block = stxlib::memory::allocate(size, alignment);
    }
    return block;
}

// Every delete: null is a no-op (stxlib::memory::release), and a block of
// the teardown reserve is left where it is.
void release_block(void* const block) noexcept {
    if (gui_forms::detail::teardown_reserve_owns(block)) {
        return;
    }
    stxlib::memory::release(block);
}

} // namespace

[[nodiscard]] void* operator new(const std::size_t size) {
    void* const block = allocate_or_throw(size, default_alignment);
    return block;
}

[[nodiscard]] void* operator new[](const std::size_t size) {
    void* const block = allocate_or_throw(size, default_alignment);
    return block;
}

[[nodiscard]] void* operator new(const std::size_t size, const std::nothrow_t&) noexcept {
    void* const block = allocate_or_null(size, default_alignment);
    return block;
}

[[nodiscard]] void* operator new[](const std::size_t size, const std::nothrow_t&) noexcept {
    void* const block = allocate_or_null(size, default_alignment);
    return block;
}

[[nodiscard]] void* operator new(const std::size_t size, const std::align_val_t alignment) {
    void* const block = allocate_or_throw(size, block_alignment(alignment));
    return block;
}

[[nodiscard]] void* operator new[](const std::size_t size, const std::align_val_t alignment) {
    void* const block = allocate_or_throw(size, block_alignment(alignment));
    return block;
}

[[nodiscard]] void* operator new(const std::size_t size, const std::align_val_t alignment,
                                 const std::nothrow_t&) noexcept {
    void* const block = allocate_or_null(size, block_alignment(alignment));
    return block;
}

[[nodiscard]] void* operator new[](const std::size_t size, const std::align_val_t alignment,
                                   const std::nothrow_t&) noexcept {
    void* const block = allocate_or_null(size, block_alignment(alignment));
    return block;
}

void operator delete(void* const block) noexcept {
    release_block(block);
}

void operator delete[](void* const block) noexcept {
    release_block(block);
}

void operator delete(void* const block, const std::nothrow_t&) noexcept {
    release_block(block);
}

void operator delete[](void* const block, const std::nothrow_t&) noexcept {
    release_block(block);
}

// The sized forms: STXlib's allocator finds a block's size itself. Defined
// even where libc++ declares no sized deallocation (MinGW clang64's), so a
// compiler that calls them never reaches libc++'s.
void operator delete(void* const block, const std::size_t) noexcept {
    release_block(block);
}

void operator delete[](void* const block, const std::size_t) noexcept {
    release_block(block);
}

void operator delete(void* const block, const std::align_val_t) noexcept {
    release_block(block);
}

void operator delete[](void* const block, const std::align_val_t) noexcept {
    release_block(block);
}

void operator delete(void* const block, const std::align_val_t, const std::nothrow_t&) noexcept {
    release_block(block);
}

void operator delete[](void* const block, const std::align_val_t, const std::nothrow_t&) noexcept {
    release_block(block);
}

void operator delete(void* const block, const std::size_t, const std::align_val_t) noexcept {
    release_block(block);
}

void operator delete[](void* const block, const std::size_t, const std::align_val_t) noexcept {
    release_block(block);
}

#endif
