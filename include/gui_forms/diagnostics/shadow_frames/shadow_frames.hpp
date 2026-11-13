#pragma once

// Shadow frames: each thread's stack of logical frames, the story of how the
// thread came to be where it is ("library call objects.create", "input
// dispatch pointer on window form.main", "event click on button.ok", "layout
// pass", "paint pass"). A debugger, a tracer or a crash report reads it where
// the native frames say nothing a program's author would recognise.
//
// Each thread owns a fixed stack of shadow_stack_capacity records, made
// with the thread and never allocated again, so a push is a few stores. A
// record holds a kind, numbers and short copied texts (a stable id, a
// name), never an address, so two runs' stacks compare. Frames are pushed
// and popped only by ShadowFrameScope, so an exception that unwinds through
// a scope pops its frame as a return would.
//
// An unwinding pop keeps the story of the failure: a popped frame's record
// stays in the stack's storage until a later push reuses it, and the first
// scope an exception unwinds notes the depth it was thrown at (two stores,
// on the exception path only). A boundary that catches the exception reads
// those frames with copy_failure_shadow_frames.

#include "gui_forms/export/application_export.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <span>
#include <string_view>

namespace gui_forms {

// What a frame records. The values are those of the C ABI's
// gf_shadow_frame_kind and never change.
enum class ShadowFrameKind : std::uint32_t {
    // A call into the library through a binding: interface_id and
    // function_index name it (for the C ABI, the table id and slot index).
    library_call = 1U,
    // A window's dispatch of one input: subject is the window root's stable
    // id, name the input's kind (pointer, key, text, drag).
    input_dispatch = 2U,
    // An event delivered to a binding's callback: subject is the sender's
    // stable id when the sender is a control (else empty), name the event's
    // name.
    event_callback = 3U,
    // A window's layout flush: subject is the window root's stable id,
    // sequence the window's first layout pass number in it.
    layout_pass = 4U,
    // A window's paint: subject is the window root's stable id, sequence the
    // window's paint lease number.
    paint_pass = 5U,
    // Work posted to the UI thread, run by a binding: subject is the
    // target's stable id when the target is a control (else empty).
    posted_callback = 6U,
};

// Set on a frame whose text was longer than its field and was cut at the
// last whole UTF-8 sequence that fits.
enum class ShadowFrameFlags : std::uint32_t {
    none = 0U,
    subject_truncated = 1U,
    name_truncated = 2U,
};

// The bytes a frame keeps of its subject and of its name.
inline constexpr std::size_t shadow_frame_subject_capacity = 48U;
inline constexpr std::size_t shadow_frame_name_capacity = 32U;
// The frames a thread records. Deeper frames are counted, not recorded.
inline constexpr std::size_t shadow_stack_capacity = 64U;

// One frame. Fields a kind does not use are zero and empty. The text arrays
// hold subject_size and name_size bytes; the bytes past them are stale.
struct GUI_FORMS_API ShadowFrame final {
    ShadowFrameKind kind = ShadowFrameKind::library_call;
    std::uint32_t flags = 0U;
    std::uint32_t interface_id = 0U;
    std::uint32_t function_index = 0U;
    std::uint64_t sequence = 0U;
    std::uint32_t subject_size = 0U;
    std::uint32_t name_size = 0U;
    std::array<char, shadow_frame_subject_capacity> subject{};
    std::array<char, shadow_frame_name_capacity> name{};
};

// The calling thread's stack beside its frames. depth counts every frame
// open now, recorded or not; the recorded frames are the outermost
// min(depth, shadow_stack_capacity). pushed and dropped count every push,
// and every push past the capacity, since the thread began.
struct GUI_FORMS_API ShadowStackState final {
    std::uint32_t depth = 0U;
    std::uint64_t pushed = 0U;
    std::uint64_t dropped = 0U;
};

// Opens a frame on the calling thread for the scope's lifetime and closes
// it on every exit, an exception's included. Never throws or allocates.
// Scopes nest strictly (they are not copied or moved), so the frame closed
// is always the innermost.
class GUI_FORMS_API ShadowFrameScope final {
public:
    // A frame of kind with subject and name (each cut to its capacity).
    ShadowFrameScope(ShadowFrameKind kind, std::string_view subject, std::string_view name,
                     std::uint64_t sequence) noexcept;
    // A library_call frame.
    ShadowFrameScope(std::uint32_t interface_id, std::uint32_t function_index) noexcept;
    // Inline on purpose: std::uncaught_exceptions() is read in the module
    // that holds the scope. Where each module links its own C++ runtime
    // (Windows, add_link_options(-static)), an exception thrown in that
    // module counts only in that module's runtime, so the library's own copy
    // would report no exception while it unwinds the scope. An exception
    // thrown in another module still unwinds the frame, but is not seen as
    // an unwinding there, so it leaves no mark.
    ~ShadowFrameScope() {
        const bool unwinding = std::uncaught_exceptions() != 0;
        close_frame(unwinding);
    }
    ShadowFrameScope(const ShadowFrameScope&) = delete;
    ShadowFrameScope& operator=(const ShadowFrameScope&) = delete;

private:
    // Pops the innermost frame; unwinding marks the depth for
    // copy_failure_shadow_frames.
    static void close_frame(bool unwinding) noexcept;
};

// Copies the calling thread's recorded frames, outermost first, into the
// leading elements of frames (at most frames.size()), and its state into
// state. Returns the number of recorded frames, which may exceed the number
// copied. Never throws or allocates.
GUI_FORMS_API std::size_t copy_shadow_frames(std::span<ShadowFrame> frames, ShadowStackState& state) noexcept;

// As copy_shadow_frames, but for a boundary that has just caught an
// exception: the frames open where the exception was thrown, the frames it
// unwound included, when no frame has been opened or closed by a return
// since its last unwinding pop; otherwise (no exception unwound a frame, or
// the thread has moved on) the frames open now. state.depth is the depth
// copied from; pushed and dropped are the thread's. Never throws or
// allocates. The mark cannot tell two unwindings apart when nothing opens
// or closes a frame between them: an exception caught and handled inside a
// frame, then another thrown from that frame or an outer one, shows the
// first's deeper frames above the second's. The outer frames are the
// second's either way.
GUI_FORMS_API std::size_t copy_failure_shadow_frames(std::span<ShadowFrame> frames, ShadowStackState& state) noexcept;

} // namespace gui_forms
