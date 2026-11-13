// GF_TABLE_DIAGNOSTICS: the calling thread's shadow frames and failure
// snapshot, and the process's debugging switches. See gf_abi.h.

#include "abi_records.hpp"
#include "abi_tables.hpp"

#include "../failure/abi_failure.hpp"
#include "../registry/abi_registry.hpp"

#include "../../../core/failure/failure_snapshot.hpp"

#include "gui_forms/diagnostics/invalidation_overlay/invalidation_overlay.hpp"
#include "gui_forms/diagnostics/shadow_frames/shadow_frames.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace gui_forms::abi1 {
namespace {

constexpr SlotIdentity shadow_frames_slot{GF_TABLE_DIAGNOSTICS, 0U};
constexpr SlotIdentity set_invalidation_overlay_slot{GF_TABLE_DIAGNOSTICS, 1U};
constexpr SlotIdentity invalidation_overlay_slot{GF_TABLE_DIAGNOSTICS, 2U};
constexpr SlotIdentity failure_snapshot_slot{GF_TABLE_DIAGNOSTICS, 3U};
constexpr SlotIdentity set_exception_reflection_slot{GF_TABLE_DIAGNOSTICS, 5U};
constexpr SlotIdentity exception_reflection_slot{GF_TABLE_DIAGNOSTICS, 6U};
constexpr SlotIdentity raise_native_exception_slot{GF_TABLE_DIAGNOSTICS, 7U};

// The 1.2 snapshot record.
constexpr std::uint32_t failure_snapshot_size_1_2 = 104U;
static_assert(sizeof(gf_failure_snapshot) == failure_snapshot_size_1_2);
static_assert(sizeof(gf_failure_snapshot{}.site) == detail::failure_site_capacity);

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

// The process switch (set_invalidation_overlay_default): any thread, no
// frame of its own.
gf_status GF_CALL diagnostics_set_invalidation_overlay(std::uint32_t enabled) noexcept {
    if (enabled > 1U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, set_invalidation_overlay_slot, "enabled must be 0 or 1");
    }
    set_invalidation_overlay_default(enabled == 1U);
    return GF_STATUS_OK;
}

gf_status GF_CALL diagnostics_invalidation_overlay(std::uint32_t* enabled) noexcept {
    if (enabled == nullptr) {
        return fail(GF_STATUS_INVALID_ARGUMENT, invalidation_overlay_slot, "invalidation_overlay needs an output");
    }
    *enabled = invalidation_overlay_default() ? 1U : 0U;
    return GF_STATUS_OK;
}

// Copies the thread's snapshot out: the record, the frames and the message,
// or, when a buffer is short, only the sizes. Opens no frame of its own.
gf_status GF_CALL diagnostics_failure_snapshot(gf_failure_snapshot* snapshot, gf_shadow_frame* frames,
                                               std::uint64_t frame_capacity, std::uint64_t* frames_required,
                                               char* message_buffer, std::uint64_t message_capacity,
                                               std::uint64_t* message_required) noexcept {
    if (frames_required == nullptr || message_required == nullptr || (frames == nullptr && frame_capacity != 0U) ||
        (message_buffer == nullptr && message_capacity != 0U)) {
        return fail(GF_STATUS_INVALID_ARGUMENT, failure_snapshot_slot,
                    "failure_snapshot needs both size outputs, and each buffer whose capacity is not zero");
    }
    const gf_status checked = check_record(snapshot, failure_snapshot_slot);
    if (checked != GF_STATUS_OK) {
        return checked;
    }
    // The thread's own storage, read on its thread; nothing below records a
    // snapshot before the copies are made.
    const detail::FailureSnapshot& source = detail::thread_failure_snapshot();
    if (frame_capacity < source.frame_count || message_capacity < source.message_size) {
        *frames_required = source.frame_count;
        *message_required = source.message_size;
        return fail(GF_STATUS_BUFFER_TOO_SMALL, failure_snapshot_slot,
                    "the frames or message buffer is smaller than the snapshot");
    }
    for (std::uint32_t index = 0U; index < source.frame_count; ++index) {
        frames[index] = c_frame(source.frames[index]);
    }
    if (source.message_size != 0U) {
        std::memcpy(message_buffer, source.message.data(), source.message_size);
    }
    gf_failure_snapshot complete{};
    complete.struct_size = sizeof(gf_failure_snapshot);
    complete.status = source.status;
    complete.table_id = source.interface_id;
    complete.slot_index = source.function_index;
    complete.sequence = source.sequence;
    complete.frame_count = source.frame_count;
    complete.depth = source.depth;
    complete.site_size = source.site_size;
    complete.message_size = source.message_size;
    std::memcpy(complete.site, source.site.data(), source.site.size());
    write_record(complete, snapshot);
    *frames_required = source.frame_count;
    *message_required = source.message_size;
    return GF_STATUS_OK;
}

gf_status GF_CALL diagnostics_clear_failure_snapshot() noexcept {
    detail::clear_failure_snapshot();
    return GF_STATUS_OK;
}

// The process switch: any thread, no frame of its own.
gf_status GF_CALL diagnostics_set_exception_reflection(std::uint32_t enabled) noexcept {
    if (enabled > 1U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, set_exception_reflection_slot, "enabled must be 0 or 1");
    }
    detail::set_exception_reflection(enabled == 1U);
    return GF_STATUS_OK;
}

gf_status GF_CALL diagnostics_exception_reflection(std::uint32_t* enabled) noexcept {
    if (enabled == nullptr) {
        return fail(GF_STATUS_INVALID_ARGUMENT, exception_reflection_slot, "exception_reflection needs an output");
    }
    *enabled = detail::exception_reflection() ? 1U : 0U;
    return GF_STATUS_OK;
}

// Throws under a frame of its own, inside the boundary every slot has, so a
// binding sees exactly what a real failure gives it.
[[noreturn]] void throw_native_exception(std::string_view message) {
    const ShadowFrameScope shadow_frame(raise_native_exception_slot.table_id, raise_native_exception_slot.slot_index);
    throw std::runtime_error(std::string(message));
}

gf_status GF_CALL diagnostics_raise_native_exception(gf_utf8_view message) noexcept {
    std::string_view text;
    const gf_status read = read_text(message, raise_native_exception_slot, text);
    if (read != GF_STATUS_OK) {
        return read;
    }
    if (text.size() > GF_FAILURE_MESSAGE_CAPACITY) {
        return fail(GF_STATUS_INVALID_ARGUMENT, raise_native_exception_slot,
                    "the message is longer than GF_FAILURE_MESSAGE_CAPACITY bytes");
    }
    try {
        throw_native_exception(text);
    } catch (...) {
        return fail_with_current_exception(raise_native_exception_slot);
    }
}

constexpr gf_diagnostics_table table{
    .struct_size = sizeof(gf_diagnostics_table),
    .abi_minor = GF_ABI_MINOR,
    .shadow_frames = &diagnostics_shadow_frames,
    .set_invalidation_overlay = &diagnostics_set_invalidation_overlay,
    .invalidation_overlay = &diagnostics_invalidation_overlay,
    .failure_snapshot = &diagnostics_failure_snapshot,
    .clear_failure_snapshot = &diagnostics_clear_failure_snapshot,
    .set_exception_reflection = &diagnostics_set_exception_reflection,
    .exception_reflection = &diagnostics_exception_reflection,
    .raise_native_exception = &diagnostics_raise_native_exception,
};

} // namespace

const gf_diagnostics_table& diagnostics_table() noexcept {
    return table;
}

} // namespace gui_forms::abi1
