#pragma once

// Shadow frames: each thread's stack of logical frames, the story of how the
// thread came to be where it is ("library call objects.create", "input
// dispatch pointer on window form.main", "event click on button.ok", "layout
// pass", "paint pass"). A debugger, a tracer or a crash report reads it where
// the native frames say nothing a program's author would recognise.
//
// The stack is STXlib's (stxlib/shadow.hpp), kept in the stxlib_state shared
// library, so every module of a process pushes onto one stack per thread and
// the gates that catch an exception (the C ABI's slots, the fail-fast
// entries, the callback gates) see the frames it was thrown under, whichever
// module threw it. Each thread records shadow_stack_capacity frames, fixed
// storage made with the thread, so a push is a few stores and a copy of two
// short texts. A frame holds a kind, numbers and short copied texts (a stable
// id, a name), never an address, so two runs' stacks compare.
//
// A frame is closed by a mark, never by asking a C++ runtime whether an
// exception is in flight: a frame left without its mark is being unwound,
// and the first frame an exception leaves captures the stack at the throw.
// The library's own code opens frames through STXlib's gates and
// ShadowFrameCall; ShadowFrameScope stays for the C++ API before 1.0.

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
    // A gate of the library's runtime (STXlib) that is not one of the
    // frames above: name is its operation, such as the site of a native
    // callback entry ("windows.window_procedure").
    gate = 7U,
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
// hold subject_size and name_size bytes; the bytes past them are zero.
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
// is always the innermost. Kept for the C++ API before 1.0; the library's
// own frames are gates and ShadowFrameCall, which need no runtime query.
class GUI_FORMS_API ShadowFrameScope final {
public:
    // A frame of kind with subject and name (each cut to its capacity).
    ShadowFrameScope(ShadowFrameKind kind, std::string_view subject, std::string_view name,
                     std::uint64_t sequence) noexcept;
    // A library_call frame.
    ShadowFrameScope(std::uint32_t interface_id, std::uint32_t function_index) noexcept;
    // Inline on purpose: std::uncaught_exceptions() is read in the module
    // that holds the scope. Where each module links its own C++ runtime
    // (Windows, add_link_options(-static)), an exception thrown in another
    // module is not counted here, so the frame closes as a return would and
    // the throw-site capture falls to an outer frame or the catching gate.
    ~ShadowFrameScope() {
        const bool unwinding = std::uncaught_exceptions() != 0;
        close_frame(mark_, unwinding);
    }
    ShadowFrameScope(const ShadowFrameScope&) = delete;
    ShadowFrameScope& operator=(const ShadowFrameScope&) = delete;

private:
    // Leaves the frame mark names; unwinding keeps the capture.
    static void close_frame(std::uint32_t mark, bool unwinding) noexcept;

    // STXlib's mark for this frame (stxlib::ShadowFrameMark::depth).
    std::uint32_t mark_ = 0U;
};

// A frame around an entry that calls into code another module may hold
// (an application's handlers and controls, under a window's input, layout
// and paint). The entry opens the frame, calls its body (a named function),
// then calls returned(); a frame destroyed before returned() is being
// unwound, whichever module threw, so no runtime is asked. The body's own
// early returns cannot skip the mark, because the mark follows the body's
// return. Never throws or allocates.
class GUI_FORMS_API ShadowFrameCall final {
public:
    ShadowFrameCall(ShadowFrameKind kind, std::string_view subject, std::string_view name,
                    std::uint64_t sequence) noexcept;
    ~ShadowFrameCall();
    ShadowFrameCall(const ShadowFrameCall&) = delete;
    ShadowFrameCall& operator=(const ShadowFrameCall&) = delete;

    // The body returned: the frame closes as a return closes it.
    void returned() noexcept { returned_ = true; }

private:
    // STXlib's mark for this frame (stxlib::ShadowFrameMark::depth).
    std::uint32_t mark_ = 0U;
    bool returned_ = false;
};

// Copies the calling thread's recorded frames, outermost first, into the
// leading elements of frames (at most frames.size()), and its state into
// state. Returns the number of recorded frames, which may exceed the number
// copied. Never throws or allocates.
GUI_FORMS_API std::size_t copy_shadow_frames(std::span<ShadowFrame> frames, ShadowStackState& state) noexcept;

// As copy_shadow_frames, but for a handler that has just caught an
// exception and is not a gate: the frames open where the exception was
// thrown, the frames it unwound included, or the frames open now when no
// frame saw the exception leave. Takes the thread's pending capture
// (stxlib::park_shadow), so call it once per caught exception, inside the
// handler. state.depth is the depth copied from; pushed and dropped are the
// thread's. Never throws or allocates.
GUI_FORMS_API std::size_t copy_failure_shadow_frames(std::span<ShadowFrame> frames, ShadowStackState& state) noexcept;

} // namespace gui_forms
