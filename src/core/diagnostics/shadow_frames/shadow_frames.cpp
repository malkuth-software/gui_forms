// Each thread's shadow stack, kept by STXlib. See shadow_frames.hpp.

#include "gui_forms/diagnostics/shadow_frames/shadow_frames.hpp"

#include "shadow_frame_records.hpp"

#include <stxlib/result.hpp>
#include <stxlib/text.hpp>
#include <stxlib/view.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

namespace gui_forms {
namespace detail {

stxlib::TextView well_formed_text(const std::string_view text) noexcept {
    if (text.empty()) {
        const stxlib::TextView none{};
        return none;
    }
    const stxlib::Result<stxlib::TextView> whole = stxlib::validate_utf8(text.data(), text.size());
    if (whole.succeeded()) {
        const stxlib::TextView valid = *whole.value();
        return valid;
    }
    // The code is the offset of the first ill-formed byte; everything before
    // it is well-formed. u32_max means the offset did not fit, which a frame
    // text (a stable id, a name) never reaches; such text records as empty.
    const stxlib::u32 offset = (*whole.error()).code();
    if (offset == 0U || offset == stxlib::u32_max || offset > text.size()) {
        const stxlib::TextView none{};
        return none;
    }
    const stxlib::Result<stxlib::TextView> prefix = stxlib::validate_utf8(text.data(), offset);
    if (!prefix.succeeded()) {
        const stxlib::TextView none{};
        return none;
    }
    const stxlib::TextView valid = *prefix.value();
    return valid;
}

ShadowFrame frame_of_record(const stxlib::ShadowFrameRecord& record) noexcept {
    ShadowFrame frame{};
    if (record.kind == stxlib::shadow_frame_kind_gate) {
        frame.kind = ShadowFrameKind::gate;
    } else {
        frame.kind = static_cast<ShadowFrameKind>(record.kind);
    }
    std::uint32_t flags = 0U;
    if ((record.flags & stxlib::shadow_subject_truncated) != 0U) {
        flags |= static_cast<std::uint32_t>(ShadowFrameFlags::subject_truncated);
    }
    if ((record.flags & stxlib::shadow_name_truncated) != 0U) {
        flags |= static_cast<std::uint32_t>(ShadowFrameFlags::name_truncated);
    }
    frame.flags = flags;
    frame.interface_id = static_cast<std::uint32_t>(record.first);
    frame.function_index = static_cast<std::uint32_t>(record.second);
    frame.sequence = record.sequence;
    // STXlib's capacities are the library's (static_asserts below); min()
    // keeps a record from another layout from overrunning the fields.
    const std::size_t subject_size = std::min<std::size_t>(record.subject_count, shadow_frame_subject_capacity);
    const std::size_t name_size = std::min<std::size_t>(record.name_count, shadow_frame_name_capacity);
    frame.subject_size = static_cast<std::uint32_t>(subject_size);
    frame.name_size = static_cast<std::uint32_t>(name_size);
    if (subject_size != 0U) {
        std::memcpy(frame.subject.data(), record.subject, subject_size);
    }
    if (name_size != 0U) {
        std::memcpy(frame.name.data(), record.name, name_size);
    }
    return frame;
}

RecordFrames copy_record_frames(const stxlib::ShadowReference reference, const std::span<ShadowFrame> frames) noexcept {
    RecordFrames result{};
    if (!reference.present()) {
        return result;
    }
    // About 7.5 KiB of stack, on a failure path only.
    std::array<stxlib::ShadowFrameRecord, shadow_stack_capacity> records{};
    const stxlib::MutableView<stxlib::ShadowFrameRecord> record_view(records.data(), records.size());
    const stxlib::MutableView<stxlib::uptr> no_trace{};
    const stxlib::Result<stxlib::ShadowSummary> read = stxlib::read_shadow(reference, record_view, no_trace);
    if (!read.succeeded()) {
        return result;
    }
    const stxlib::ShadowSummary& summary = *read.value();
    const std::size_t recorded = std::min<std::size_t>(summary.frame_count, records.size());
    const std::size_t copied = std::min(recorded, frames.size());
    for (std::size_t index = 0U; index < copied; ++index) {
        frames[index] = frame_of_record(records[index]);
    }
    result.recorded = recorded;
    result.depth = static_cast<std::uint32_t>(summary.frame_count + summary.frames_lost);
    return result;
}

} // namespace detail

namespace {

static_assert(stxlib::shadow_frame_capacity == shadow_stack_capacity);
static_assert(stxlib::shadow_subject_capacity == shadow_frame_subject_capacity);
static_assert(stxlib::shadow_name_capacity == shadow_frame_name_capacity);

// Opens a frame of kind with subject and name on STXlib's stack and
// returns its mark.
std::uint32_t enter_frame(const ShadowFrameKind kind, const std::string_view subject, const std::string_view name,
                          const std::uint64_t first, const std::uint64_t second,
                          const std::uint64_t sequence) noexcept {
    const stxlib::ShadowFrameText frame{
        .kind = static_cast<stxlib::u32>(kind),
        .subject = detail::well_formed_text(subject),
        .name = detail::well_formed_text(name),
        .first = first,
        .second = second,
        .sequence = sequence,
    };
    const stxlib::ShadowFrameMark mark = stxlib::enter_shadow_frame(frame);
    return mark.depth;
}

void leave_frame(const std::uint32_t depth, const bool closed) noexcept {
    const stxlib::ShadowFrameMark mark{.depth = depth};
    stxlib::leave_shadow_frame(mark, closed);
}

// Copies the open frames' records into frames and fills state.
std::size_t copy_open_frames(const std::span<ShadowFrame> frames, ShadowStackState& state) noexcept {
    std::array<stxlib::ShadowFrameRecord, shadow_stack_capacity> records{};
    const stxlib::MutableView<stxlib::ShadowFrameRecord> record_view(records.data(), records.size());
    const stxlib::ShadowStackState stack = stxlib::read_shadow_stack(record_view);
    const std::size_t recorded = std::min<std::size_t>(stack.recorded, records.size());
    const std::size_t copied = std::min(recorded, frames.size());
    for (std::size_t index = 0U; index < copied; ++index) {
        frames[index] = detail::frame_of_record(records[index]);
    }
    state.depth = static_cast<std::uint32_t>(stack.depth);
    state.pushed = stack.pushes;
    state.dropped = stack.dropped;
    return recorded;
}

} // namespace

ShadowFrameScope::ShadowFrameScope(const ShadowFrameKind kind, const std::string_view subject,
                                   const std::string_view name, const std::uint64_t sequence) noexcept
    : mark_(enter_frame(kind, subject, name, 0U, 0U, sequence)) {}

ShadowFrameScope::ShadowFrameScope(const std::uint32_t interface_id, const std::uint32_t function_index) noexcept
    : mark_(enter_frame(ShadowFrameKind::library_call, std::string_view{}, std::string_view{}, interface_id,
                        function_index, 0U)) {}

void ShadowFrameScope::close_frame(const std::uint32_t mark, const bool unwinding) noexcept {
    leave_frame(mark, !unwinding);
}

ShadowFrameCall::ShadowFrameCall(const ShadowFrameKind kind, const std::string_view subject,
                                 const std::string_view name, const std::uint64_t sequence) noexcept
    : mark_(enter_frame(kind, subject, name, 0U, 0U, sequence)) {}

ShadowFrameCall::~ShadowFrameCall() {
    leave_frame(mark_, returned_);
}

std::size_t copy_shadow_frames(const std::span<ShadowFrame> frames, ShadowStackState& state) noexcept {
    const std::size_t recorded = copy_open_frames(frames, state);
    return recorded;
}

std::size_t copy_failure_shadow_frames(const std::span<ShadowFrame> frames, ShadowStackState& state) noexcept {
    // The thread's counts as the handler finds them.
    const std::size_t open = copy_open_frames(frames, state);
    const stxlib::ShadowReference parked = stxlib::park_shadow();
    if (!parked.present()) {
        return open;
    }
    const detail::RecordFrames read = detail::copy_record_frames(parked, frames);
    state.depth = read.depth;
    return read.recorded;
}

} // namespace gui_forms
