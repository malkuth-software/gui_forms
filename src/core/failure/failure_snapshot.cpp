// Each thread's failure snapshot. See failure_snapshot.hpp.

#include "failure_snapshot.hpp"

#include "../diagnostics/shadow_frames/shadow_frame_records.hpp"

#include <stxlib/shadow.hpp>
#include <stxlib/text_view.hpp>
#include <stxlib/view.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <span>
#include <string_view>

namespace gui_forms::detail {
namespace {

thread_local FailureSnapshot thread_snapshot;

// Relaxed: the switch orders nothing else; a call that starts after the
// switch changes sees the new value.
std::atomic<bool> reflection_enabled{false};

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

// Copies the whole prefix of text into field, zeroes the rest, and returns
// the bytes copied.
template <std::size_t Capacity>
std::uint32_t copy_text(std::string_view text, std::array<char, Capacity>& field) noexcept {
    const std::size_t size = whole_prefix(text, Capacity);
    field.fill('\0');
    if (size != 0U) {
        std::memcpy(field.data(), text.data(), size);
    }
    return static_cast<std::uint32_t>(size);
}

// Fills everything but the frames and the trace, which the caller has
// written.
void fill_snapshot(FailureSnapshot& snapshot, const FailureBoundary& boundary, const std::string_view message,
                   const std::size_t recorded, const std::uint32_t depth, const std::size_t trace_count,
                   const std::string_view type) noexcept {
    snapshot.status = boundary.status;
    snapshot.interface_id = boundary.interface_id;
    snapshot.function_index = boundary.function_index;
    ++snapshot.sequence;
    snapshot.frame_count = static_cast<std::uint32_t>(std::min<std::size_t>(recorded, shadow_stack_capacity));
    snapshot.depth = depth;
    snapshot.site_size = copy_text(boundary.site, snapshot.site);
    snapshot.message_size = copy_text(message, snapshot.message);
    snapshot.trace_count = static_cast<std::uint32_t>(std::min<std::size_t>(trace_count, failure_trace_capacity));
    for (std::size_t index = snapshot.trace_count; index < snapshot.trace.size(); ++index) {
        snapshot.trace[index] = 0U;
    }
    snapshot.type_size = copy_text(type, snapshot.type);
}

} // namespace

void record_failure_snapshot(const FailureBoundary& boundary, const std::string_view message,
                             const stxlib::ErrorRecord& record) noexcept {
    FailureSnapshot& snapshot = thread_snapshot;
    const RecordFrames read = copy_record_frames(record.shadow(), std::span<ShadowFrame>(snapshot.frames),
                                                 std::span<std::uint64_t>(snapshot.trace));
    // Demangled here, when the failure is filed, never at the capture
    // (exception_type_for_display: one __cxa_demangle call, which allocates
    // and frees its result). The view borrows type_storage or record.
    std::array<char, stxlib::exception_type_display_capacity> type_storage{};
    const stxlib::MutableView<char> type_output(type_storage.data(), type_storage.size());
    const stxlib::TextView type = stxlib::exception_type_for_display(record, type_output);
    const std::string_view type_text(type.data(), type.count());
    fill_snapshot(snapshot, boundary, message, read.recorded, read.depth, read.trace_count, type_text);
}

void record_failure_snapshot(const FailureBoundary& boundary, const std::string_view message) noexcept {
    FailureSnapshot& snapshot = thread_snapshot;
    // The frames open now, in case no capture is pending.
    ShadowStackState state{};
    std::size_t recorded = copy_shadow_frames(std::span<ShadowFrame>(snapshot.frames), state);
    std::uint32_t depth = state.depth;
    std::size_t trace_count = 0U;
    const stxlib::ShadowReference parked = stxlib::park_shadow();
    if (parked.present()) {
        const RecordFrames read = copy_record_frames(parked, std::span<ShadowFrame>(snapshot.frames),
                                                     std::span<std::uint64_t>(snapshot.trace));
        recorded = read.recorded;
        depth = read.depth;
        trace_count = read.trace_count;
    }
    fill_snapshot(snapshot, boundary, message, recorded, depth, trace_count, std::string_view{});
}

void record_failure_snapshot(const FailureBoundary& boundary, const std::exception_ptr failure) noexcept {
    if (!failure) {
        record_failure_snapshot(boundary, std::string_view("no exception"));
        return;
    }
    // what() stays valid while failure holds the exception, which outlives
    // the record below.
    std::string_view message = "an exception not derived from std::exception";
    try {
        std::rethrow_exception(failure);
    } catch (const std::exception& error) {
        message = error.what();
    } catch (...) {
        // Not a std::exception: the fixed text above stands.
    }
    record_failure_snapshot(boundary, message);
}

const FailureSnapshot& thread_failure_snapshot() noexcept {
    return thread_snapshot;
}

void clear_failure_snapshot() noexcept {
    FailureSnapshot& snapshot = thread_snapshot;
    snapshot.status = 0;
    snapshot.interface_id = 0U;
    snapshot.function_index = 0U;
    snapshot.frame_count = 0U;
    snapshot.depth = 0U;
    snapshot.site_size = 0U;
    snapshot.message_size = 0U;
    snapshot.site.fill('\0');
    snapshot.message.fill('\0');
    snapshot.type_size = 0U;
    snapshot.trace_count = 0U;
    snapshot.type.fill('\0');
    snapshot.trace.fill(0U);
}

void set_exception_reflection(const bool enabled) noexcept {
    reflection_enabled.store(enabled, std::memory_order_relaxed);
}

bool exception_reflection() noexcept {
    return reflection_enabled.load(std::memory_order_relaxed);
}

} // namespace gui_forms::detail
