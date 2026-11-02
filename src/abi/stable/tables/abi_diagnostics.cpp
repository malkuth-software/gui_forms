// GF_TABLE_DIAGNOSTICS: the calling thread's shadow frames. See gf_abi.h.

#include "abi_records.hpp"
#include "abi_tables.hpp"

#include "../failure/abi_failure.hpp"

#include "gui_forms/diagnostics/shadow_frames/shadow_frames.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace gui_forms::abi1 {
namespace {

constexpr SlotIdentity shadow_frames_slot{GF_TABLE_DIAGNOSTICS, 0U};

// The 1.0 sizes: the fixed frame stride and the state record.
constexpr std::size_t shadow_frame_size_1_0 = 112U;
constexpr std::uint32_t shadow_stack_info_size_1_0 = 24U;
static_assert(sizeof(gf_shadow_frame) == shadow_frame_size_1_0);
static_assert(sizeof(gf_shadow_stack_info) == shadow_stack_info_size_1_0);

// The C record's capacities and kind values are the core's.
static_assert(GF_SHADOW_FRAME_SUBJECT_CAPACITY == shadow_frame_subject_capacity);
static_assert(GF_SHADOW_FRAME_NAME_CAPACITY == shadow_frame_name_capacity);
static_assert(GF_SHADOW_STACK_CAPACITY == shadow_stack_capacity);
static_assert(sizeof(gf_shadow_frame{}.subject) == shadow_frame_subject_capacity);
static_assert(sizeof(gf_shadow_frame{}.name) == shadow_frame_name_capacity);
static_assert(GF_SHADOW_FRAME_KIND_LIBRARY_CALL == static_cast<std::uint32_t>(ShadowFrameKind::library_call));
static_assert(GF_SHADOW_FRAME_KIND_INPUT_DISPATCH == static_cast<std::uint32_t>(ShadowFrameKind::input_dispatch));
static_assert(GF_SHADOW_FRAME_KIND_EVENT_CALLBACK == static_cast<std::uint32_t>(ShadowFrameKind::event_callback));
static_assert(GF_SHADOW_FRAME_KIND_LAYOUT_PASS == static_cast<std::uint32_t>(ShadowFrameKind::layout_pass));
static_assert(GF_SHADOW_FRAME_KIND_PAINT_PASS == static_cast<std::uint32_t>(ShadowFrameKind::paint_pass));
static_assert(GF_SHADOW_FRAME_KIND_POSTED_CALLBACK == static_cast<std::uint32_t>(ShadowFrameKind::posted_callback));
static_assert(GF_SHADOW_FRAME_SUBJECT_TRUNCATED == static_cast<std::uint32_t>(ShadowFrameFlags::subject_truncated));
static_assert(GF_SHADOW_FRAME_NAME_TRUNCATED == static_cast<std::uint32_t>(ShadowFrameFlags::name_truncated));

// One frame as the C record, its unused text bytes zero.
gf_shadow_frame c_frame(const ShadowFrame& frame) noexcept {
    gf_shadow_frame record{};
    record.kind = static_cast<gf_shadow_frame_kind>(frame.kind);
    record.flags = frame.flags;
    record.table_id = frame.interface_id;
    record.slot_index = frame.function_index;
    record.sequence = frame.sequence;
    record.subject_size = frame.subject_size;
    record.name_size = frame.name_size;
    std::memcpy(record.subject, frame.subject.data(), frame.subject_size);
    std::memcpy(record.name, frame.name.data(), frame.name_size);
    return record;
}

// Opens no frame of its own, so the copy shows the caller's stack.
gf_status GF_CALL diagnostics_shadow_frames(gf_shadow_frame* frames, std::uint64_t capacity, std::uint64_t* required,
                                            gf_shadow_stack_info* info) noexcept {
    if (required == nullptr || (frames == nullptr && capacity != 0U)) {
        return fail(GF_STATUS_INVALID_ARGUMENT, shadow_frames_slot,
                    "shadow_frames needs a required output, and frames when capacity is not zero");
    }
    const gf_status checked = check_record(info, shadow_frames_slot);
    if (checked != GF_STATUS_OK) {
        return checked;
    }
    // The thread's frames are copied whole first: the copy is a snapshot,
    // and nothing below opens a frame.
    std::array<ShadowFrame, shadow_stack_capacity> snapshot;
    ShadowStackState state{};
    const std::size_t recorded = copy_shadow_frames(std::span<ShadowFrame>(snapshot), state);
    if (capacity < recorded) {
        *required = recorded;
        return fail(GF_STATUS_BUFFER_TOO_SMALL, shadow_frames_slot, "the frames buffer is smaller than the stack");
    }
    for (std::size_t index = 0U; index < recorded; ++index) {
        frames[index] = c_frame(snapshot[index]);
    }
    gf_shadow_stack_info complete{};
    complete.struct_size = sizeof(gf_shadow_stack_info);
    complete.depth = state.depth;
    complete.pushed = state.pushed;
    complete.dropped = state.dropped;
    write_record(complete, info);
    *required = recorded;
    return GF_STATUS_OK;
}

constexpr gf_diagnostics_table table{
    .struct_size = sizeof(gf_diagnostics_table),
    .abi_minor = GF_ABI_MINOR,
    .shadow_frames = &diagnostics_shadow_frames,
};

} // namespace

const gf_diagnostics_table& diagnostics_table() noexcept {
    return table;
}

} // namespace gui_forms::abi1
