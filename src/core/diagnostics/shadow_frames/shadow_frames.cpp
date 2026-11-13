// Each thread's shadow stack. See shadow_frames.hpp.

#include "gui_forms/diagnostics/shadow_frames/shadow_frames.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

namespace gui_forms {
namespace {

// One thread's stack. It is zero-initialized thread storage, made with the
// thread, so no push allocates.
//
// The unwind mark: unwind_depth is the depth at the first pop of the latest
// unwinding (the depth the exception was thrown at, as far as frames go).
// unwind_pushed and unwind_next_depth are pushed and the depth just after
// that unwinding's latest pop; while both still equal the stack's, no frame
// has been opened or closed by a return since, so the records up to
// unwind_depth are intact and belong to that exception.
struct ShadowStack final {
    std::array<ShadowFrame, shadow_stack_capacity> frames{};
    std::uint32_t depth = 0U;
    std::uint64_t pushed = 0U;
    std::uint64_t dropped = 0U;
    std::uint32_t unwind_depth = 0U;
    std::uint32_t unwind_next_depth = 0U;
    std::uint64_t unwind_pushed = 0U;
};

thread_local ShadowStack thread_stack;

// The leading bytes of text that fit in capacity and end on a whole UTF-8
// sequence.
std::size_t whole_prefix(std::string_view text, std::size_t capacity) noexcept {
    if (text.size() <= capacity) {
        return text.size();
    }
    std::size_t size = capacity;
    // Back up over continuation bytes to the lead byte of the cut sequence.
    while (size > 0U && (static_cast<unsigned char>(text[size]) & 0xC0U) == 0x80U) {
        --size;
    }
    return size;
}

// The frame to record into, or null when the stack is past its capacity.
// Counts the push either way.
ShadowFrame* open_frame() noexcept {
    ShadowStack& stack = thread_stack;
    ++stack.pushed;
    const std::uint32_t index = stack.depth;
    ++stack.depth;
    if (index >= shadow_stack_capacity) {
        ++stack.dropped;
        return nullptr;
    }
    return &stack.frames[index];
}

} // namespace

ShadowFrameScope::ShadowFrameScope(const ShadowFrameKind kind, const std::string_view subject,
                                   const std::string_view name, const std::uint64_t sequence) noexcept {
    ShadowFrame* frame = open_frame();
    if (frame == nullptr) {
        return;
    }
    const std::size_t subject_size = whole_prefix(subject, shadow_frame_subject_capacity);
    const std::size_t name_size = whole_prefix(name, shadow_frame_name_capacity);
    std::uint32_t flags = 0U;
    if (subject_size != subject.size()) {
        flags |= static_cast<std::uint32_t>(ShadowFrameFlags::subject_truncated);
    }
    if (name_size != name.size()) {
        flags |= static_cast<std::uint32_t>(ShadowFrameFlags::name_truncated);
    }
    (*frame).kind = kind;
    (*frame).flags = flags;
    (*frame).interface_id = 0U;
    (*frame).function_index = 0U;
    (*frame).sequence = sequence;
    (*frame).subject_size = static_cast<std::uint32_t>(subject_size);
    (*frame).name_size = static_cast<std::uint32_t>(name_size);
    if (subject_size != 0U) {
        std::memcpy((*frame).subject.data(), subject.data(), subject_size);
    }
    if (name_size != 0U) {
        std::memcpy((*frame).name.data(), name.data(), name_size);
    }
}

ShadowFrameScope::ShadowFrameScope(const std::uint32_t interface_id, const std::uint32_t function_index) noexcept {
    ShadowFrame* frame = open_frame();
    if (frame == nullptr) {
        return;
    }
    (*frame).kind = ShadowFrameKind::library_call;
    (*frame).flags = 0U;
    (*frame).interface_id = interface_id;
    (*frame).function_index = function_index;
    (*frame).sequence = 0U;
    (*frame).subject_size = 0U;
    (*frame).name_size = 0U;
}

void ShadowFrameScope::close_frame(const bool unwinding) noexcept {
    ShadowStack& stack = thread_stack;
    // An exception is unwinding this scope. The first such pop of an
    // unwinding marks the depth; later pops of the same unwinding find the
    // mark current (nothing pushed, the depth where the last pop left it)
    // and only move it along.
    if (unwinding) {
        const bool continuing = stack.unwind_pushed == stack.pushed && stack.unwind_next_depth == stack.depth;
        if (!continuing) {
            stack.unwind_depth = stack.depth;
            stack.unwind_pushed = stack.pushed;
        }
        // Scopes nest strictly, so the stack is never empty here.
        stack.unwind_next_depth = stack.depth - 1U;
    }
    --stack.depth;
}

namespace {

// Copies the outermost recorded frames below depth and fills state.
std::size_t copy_frames_below(const ShadowStack& stack, const std::uint32_t depth, const std::span<ShadowFrame> frames,
                              ShadowStackState& state) noexcept {
    const std::size_t recorded = std::min<std::size_t>(depth, shadow_stack_capacity);
    const std::size_t copied = std::min(recorded, frames.size());
    for (std::size_t index = 0U; index < copied; ++index) {
        frames[index] = stack.frames[index];
    }
    state.depth = depth;
    state.pushed = stack.pushed;
    state.dropped = stack.dropped;
    return recorded;
}

} // namespace

std::size_t copy_shadow_frames(const std::span<ShadowFrame> frames, ShadowStackState& state) noexcept {
    const ShadowStack& stack = thread_stack;
    return copy_frames_below(stack, stack.depth, frames, state);
}

std::size_t copy_failure_shadow_frames(const std::span<ShadowFrame> frames, ShadowStackState& state) noexcept {
    const ShadowStack& stack = thread_stack;
    const bool mark_current = stack.unwind_pushed == stack.pushed && stack.unwind_next_depth == stack.depth &&
        stack.unwind_depth > stack.depth;
    const std::uint32_t depth = mark_current ? stack.unwind_depth : stack.depth;
    return copy_frames_below(stack, depth, frames, state);
}

} // namespace gui_forms
