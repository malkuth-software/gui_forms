// The opt-in whole-program allocator: the global operator new and delete of
// a program that asks for it, over STXlib's allocator (stxlib/memory.hpp:
// stx_minmallocpp by default).
//
// Never compiled into GUI.Forms itself, and never into a program by
// default (owner, 2026-11-22: GUI.Forms' own code uses its allocator,
// programs that use GUI.Forms keep theirs, and the programs GUI.Forms'
// projects issue may opt in). A program opts in with the CMake function
// gui_forms_use_library_allocator(target), which adds this file to the
// program's sources, so it is built with the program's own flags, and its
// definitions, being in an object file, take precedence over libc++'s,
// which libc++ defines weak ([replacement.functions]). The program is one
// module (libc++ is linked statically into it), so every C++ allocation in
// it then comes from STXlib's allocator: the program's, libc++'s and
// GUI.Forms'. A shared library or plugin must not opt in: it would replace
// operator new for the host process that loads it.
//
// Left out (operator new stays the toolchain's) under a sanitizer that
// intercepts the host allocator, or when the program defines
// GUI_FORMS_HOST_OPERATOR_NEW. The malloc family is never replaced.
//
// Every form of [new.delete.single] and [new.delete.array] is replaced, the
// sized and aligned deletes included, so that no pair can mix this
// allocator with libc++'s. The program's allocations do not pass through
// GUI.Forms' library seam (allocation/library_memory): its counts and test
// observer stay the library's.
//
// Failure ([new.delete.single]p3): STXlib's allocator never calls a
// new-handler and never throws. This file does both: on a null block it
// calls the installed new-handler and retries; without one the throwing
// forms throw std::bad_alloc (or abort in a program built without
// exceptions) and the nothrow forms return null.
//
// Threads and the process's ends: STXlib's allocator serves any thread at
// any time, before main (its state is constant-initialised), during a
// thread's exit and during static destruction. On Windows, once
// ExitProcess has stopped the other threads, an allocation the calling
// thread's cached blocks cannot serve returns null (stxlib/memory.hpp,
// "Process end"); a program's static destructors run before that.

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
// back or no handler is installed.
[[nodiscard]] void* allocate_or_throw(const std::size_t size, const std::size_t alignment) {
    void* block = stxlib::memory::allocate(size, alignment);
    while (block == nullptr) {
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

// Every delete: null is a no-op (stxlib::memory::release).
void release_block(void* const block) noexcept {
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
