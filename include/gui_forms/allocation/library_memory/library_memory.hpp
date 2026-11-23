#pragma once

// The allocator of GUI.Forms' own code (owner, 2026-11-22: "code inside
// gui.forms should use our allocator. programs that use gui.forms might use
// their own allocator, but instantiating our code, uses our allocator").
//
// Every allocation GUI.Forms' code makes goes to STXlib's allocator
// (stxlib/memory.hpp: stx_minmallocpp, or the host's C library in sanitizer
// builds, as STXlib chooses), whatever the program's own operator new is.
// GUI.Forms replaces nothing global: neither operator new and delete nor
// the malloc family, so the program's own allocations stay the program's.
//
// The routes, each releasing to the allocator that allocated, so that the
// side that allocated frees and no caller can mix them:
// - LibraryAllocated: the class-scope operator new and delete of GUI.Forms'
//   object roots (Component, so every control and component, and
//   SurfaceRoot, so every window). `new Window(...)`, `std::make_unique`,
//   and a program's subclass of a control take their storage here, and the
//   delete-expression, std::default_delete included, releases it here,
//   through the class's virtual destructor;
// - make_library_shared: std::allocate_shared with LibraryAllocator, for
//   every shared object GUI.Forms makes (make_control among them). The
//   control block keeps the allocator, so the last owner, a program's
//   std::shared_ptr included, releases to STXlib's allocator;
// - LibraryAllocator: the allocator type of a container whose storage is
//   GUI.Forms' (CountedAllocator adds counting to it);
// - library_allocate and library_release: raw storage.
// The C ABI's objects are handles whose create and release calls run in the
// library, and the C# wrappers hold those handles, so neither side ever
// frees the other's memory.
//
// The standard containers, strings and plain news inside GUI.Forms stay as
// they are (owner, 2026-11-17: when GUI.Forms uses std::string it is on its
// side of the shield). Their allocations follow the module they run in:
// - in the C ABI library, the module's own operator new and delete, kept
//   local to it (src/runtime/program_allocator/
//   program_allocator_replacement.cpp; cmake/CAbi.cmake): STXlib's
//   allocator, for every allocation inside the module;
// - in a program that links GUI.Forms statically, one module with one
//   libc++: the program's operator new, which is the host's unless the
//   program opts in (below). The routes above keep GUI.Forms' own objects on
//   STXlib's allocator there too.
//
// Programs GUI.Forms' own projects issue (owner, 2026-11-22: "for programs
// we issue ... they indeed can use our allocator") may opt in, never by
// default: a container of their own with LibraryAllocator, or the whole
// program with the CMake function gui_forms_use_library_allocator(target),
// which compiles the same replacement into that program (left out under a
// sanitizer). A
// third-party program keeps its own allocator unless it asks.
//
// A module's end (the C ABI library's unload, and the process's end). On
// Windows the library's static destructors run at its DLL_PROCESS_DETACH,
// under the loader lock and, at process end, after ExitProcess has stopped
// every other thread, when STXlib's allocator refuses whatever the calling
// thread's cached blocks cannot serve (stxlib/memory.hpp, "Process end").
// A static destructor must never meet std::bad_alloc there, so the module
// marks its teardown (begin_library_teardown, from its DllMain:
// src/surface/windows/abi/c_abi_module_teardown.cpp) and from then on:
// - every release succeeds: STXlib's release returns at process end (it
//   frees, or leaves the block to the ending process), and a block from the
//   teardown reserve is left where it is;
// - an allocation STXlib's allocator refuses comes from the teardown
//   reserve, a fixed static block of this module; one the reserve cannot
//   hold is a defect and ends the process through stxlib::fail_fast with an
//   out_of_memory record (gf.allocation.teardown_reserve), never a thrown
//   std::bad_alloc.
// On Linux and macOS no thread is stopped at exit or dlclose, so STXlib's
// allocator serves static destructors as it serves any code; only true
// exhaustion refuses there, as everywhere.
//
// A library allocation passes through one out-of-line seam,
// detail::library_allocate, which keeps per-thread counts and admits a
// test-only observer that may refuse it, so tests that count or fail the
// program's allocations see the library's in the same order.

#include "gui_forms/export/application_export.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace gui_forms {

namespace detail {

// `size` bytes aligned to `alignment` (a power of two, never less than
// operator new's default is asked for by the callers below) from STXlib's
// allocator; null when it refuses, or when the observer refuses, except
// after the module's teardown began, when the teardown reserve serves it
// (below). Never throws and never calls a new-handler. A size of zero yields a unique
// block.
[[nodiscard]] GUI_FORMS_API void* library_allocate(std::size_t size, std::size_t alignment) noexcept;

// Releases a block from library_allocate; null and a block of the teardown
// reserve are no-ops. A block from anywhere else ends the process
// (stxlib/memory.hpp).
GUI_FORMS_API void library_release(void* block) noexcept;

// The test-only observer: called with the size before each library
// allocation, from the allocating thread; false refuses the allocation (the
// caller then sees exhaustion: std::bad_alloc from the routes above). Must
// not allocate through GUI.Forms, throw or block. Null, the default,
// observes nothing. Process-wide; set it before the threads it should see
// start.
using LibraryAllocationObserver = bool (*)(std::size_t size) noexcept;
GUI_FORMS_API void set_library_allocation_observer(LibraryAllocationObserver observer) noexcept;

// The calling thread's library allocations granted and blocks released
// since it started. Neither is reset.
[[nodiscard]] GUI_FORMS_API std::uint64_t library_allocations_granted() noexcept;
[[nodiscard]] GUI_FORMS_API std::uint64_t library_blocks_released() noexcept;

// Marks the module's teardown (above); never undone. Called by the C ABI
// library's DllMain at DLL_PROCESS_DETACH, and by tests.
GUI_FORMS_API void begin_library_teardown() noexcept;

// Whether begin_library_teardown has been called.
[[nodiscard]] GUI_FORMS_API bool library_teardown_begun() noexcept;

// A block of `size` bytes aligned to `alignment` (a power of two) from the
// teardown reserve, for an allocation STXlib's allocator refused after the
// teardown began. Never null and never throws: when the reserve cannot hold
// it, ends the process through stxlib::fail_fast. Its blocks are never
// reused.
[[nodiscard]] GUI_FORMS_API void* teardown_allocate(std::size_t size, std::size_t alignment) noexcept;

// Whether `block` lies in the teardown reserve; releasing such a block does
// nothing.
[[nodiscard]] GUI_FORMS_API bool teardown_reserve_owns(const void* block) noexcept;

// The bytes of the teardown reserve handed out so far, and its capacity.
[[nodiscard]] GUI_FORMS_API std::size_t teardown_reserve_used() noexcept;
[[nodiscard]] GUI_FORMS_API std::size_t teardown_reserve_capacity() noexcept;

// The alignment a library block of `alignment` is asked for: never less
// than operator new's default.
[[nodiscard]] constexpr std::size_t library_alignment(const std::size_t alignment) noexcept {
    if (alignment < __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
        return __STDCPP_DEFAULT_NEW_ALIGNMENT__;
    }
    return alignment;
}

// library_allocate, or std::bad_alloc when it is refused.
[[nodiscard]] inline void* library_allocate_or_throw(const std::size_t size, const std::size_t alignment) {
    void* const block = library_allocate(size, library_alignment(alignment));
    if (block == nullptr) {
        throw std::bad_alloc();
    }
    return block;
}

} // namespace detail

// The allocator type of storage GUI.Forms owns, and of a program's own
// containers that opt in to GUI.Forms' allocator: detail::library_allocate
// and detail::library_release. Stateless (is_always_equal), so containers move storage
// between instances freely; usable for an incomplete value type, as nothing
// depends on the value type before allocate is instantiated.
template <typename Value>
// Not final: libstdc++'s containers derive from their allocator.
class LibraryAllocator {
public:
    using value_type = Value;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using is_always_equal = std::true_type;
    using propagate_on_container_move_assignment = std::true_type;

    template <typename Other>
    struct rebind {
        using other = LibraryAllocator<Other>;
    };

    LibraryAllocator() noexcept {}
    template <typename Other>
    LibraryAllocator(const LibraryAllocator<Other>&) noexcept {}

    // Storage for `count` values, uninitialized. std::bad_array_new_length
    // when count * sizeof(Value) overflows; std::bad_alloc when refused.
    [[nodiscard]] Value* allocate(std::size_t count) {
        if (count > std::numeric_limits<std::size_t>::max() / sizeof(Value)) {
            throw std::bad_array_new_length();
        }
        const std::size_t bytes = count * sizeof(Value);
        void* const memory = detail::library_allocate_or_throw(bytes, alignof(Value));
        Value* const values = static_cast<Value*>(memory);
        return values;
    }

    void deallocate(Value* values, std::size_t) noexcept {
        detail::library_release(static_cast<void*>(values));
    }
};

template <typename Left, typename Right>
[[nodiscard]] constexpr bool operator==(const LibraryAllocator<Left>&, const LibraryAllocator<Right>&) noexcept {
    return true;
}

template <typename Left, typename Right>
[[nodiscard]] constexpr bool operator!=(const LibraryAllocator<Left>&, const LibraryAllocator<Right>&) noexcept {
    return false;
}

namespace detail {

// A shared object of GUI.Forms': the object and its control block in one
// block from LibraryAllocator, released to it by whichever owner is last.
template <typename Value, typename... Arguments>
[[nodiscard]] std::shared_ptr<Value> make_library_shared(Arguments&&... arguments) {
    using Stored = std::remove_cv_t<Value>;
    const LibraryAllocator<Stored> allocator{};
    std::shared_ptr<Value> made = std::allocate_shared<Value>(allocator, std::forward<Arguments>(arguments)...);
    return made;
}

} // namespace detail

// The class-scope allocation functions of GUI.Forms' object roots: every
// form of operator new and delete a new- or delete-expression of a derived
// type can name, over the library seam, so that the object's storage is
// GUI.Forms' whoever writes the expression. The placement form stays the
// ordinary one (a class-scope operator new hides the global placement new
// otherwise). Empty: deriving from it adds no state.
class GUI_FORMS_API LibraryAllocated {
public:
    [[nodiscard]] static void* operator new(const std::size_t size) {
        void* const block = detail::library_allocate_or_throw(size, __STDCPP_DEFAULT_NEW_ALIGNMENT__);
        return block;
    }
    [[nodiscard]] static void* operator new[](const std::size_t size) {
        void* const block = detail::library_allocate_or_throw(size, __STDCPP_DEFAULT_NEW_ALIGNMENT__);
        return block;
    }
    [[nodiscard]] static void* operator new(const std::size_t size, const std::align_val_t alignment) {
        void* const block = detail::library_allocate_or_throw(size, static_cast<std::size_t>(alignment));
        return block;
    }
    [[nodiscard]] static void* operator new[](const std::size_t size, const std::align_val_t alignment) {
        void* const block = detail::library_allocate_or_throw(size, static_cast<std::size_t>(alignment));
        return block;
    }
    [[nodiscard]] static void* operator new(const std::size_t size, const std::nothrow_t&) noexcept {
        void* const block = detail::library_allocate(size, __STDCPP_DEFAULT_NEW_ALIGNMENT__);
        return block;
    }
    [[nodiscard]] static void* operator new(const std::size_t size, const std::align_val_t alignment,
                                            const std::nothrow_t&) noexcept {
        const std::size_t asked = detail::library_alignment(static_cast<std::size_t>(alignment));
        void* const block = detail::library_allocate(size, asked);
        return block;
    }
    [[nodiscard]] static void* operator new(const std::size_t, void* const place) noexcept {
        return place;
    }

    static void operator delete(void* const block) noexcept {
        detail::library_release(block);
    }
    static void operator delete[](void* const block) noexcept {
        detail::library_release(block);
    }
    static void operator delete(void* const block, const std::align_val_t) noexcept {
        detail::library_release(block);
    }
    static void operator delete[](void* const block, const std::align_val_t) noexcept {
        detail::library_release(block);
    }
    static void operator delete(void* const block, const std::nothrow_t&) noexcept {
        detail::library_release(block);
    }
    static void operator delete(void* const block, const std::align_val_t, const std::nothrow_t&) noexcept {
        detail::library_release(block);
    }
    static void operator delete(void*, void*) noexcept {}
};

} // namespace gui_forms
