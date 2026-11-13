#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/diagnostics/shadow_frames/shadow_frames.hpp"

#include <stxlib/error.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <string_view>

// Failure snapshots (C ABI 1.2, gf_failure_snapshot). Where an exception
// reaches a boundary, the boundary copies, on its own thread, what failed:
// its site, its text and the shadow frames it was thrown under (the frames
// it unwound included), read from STXlib's shadow record of the exception.
// Two boundaries take one: a C ABI function refusing its call (the slot gate,
// abi1::call_slot, and the few handlers that keep the exception) and the
// fail-fast boundary (call_failing_fast), whose report prints it before the
// process ends.
//
// Each thread owns its snapshot (thread_local, about 8 KiB, made with the
// thread), so taking one never allocates and cannot fail; a thread's next
// snapshot replaces it. Readers get copies (the C ABI's failure_snapshot);
// the storage is never lent past this module's callers on the same thread.
//
// Exception reflection is a process switch, off at start, read by the C ABI
// boundary only: on, an exception the library has no status for is refused
// with GF_STATUS_NATIVE_EXCEPTION instead of GF_STATUS_INTERNAL. The
// fail-fast boundary ends the process either way.

namespace gui_forms::detail {

// The bytes a snapshot keeps of its site and of its message; longer texts
// are cut at the last whole UTF-8 sequence that fits.
inline constexpr std::size_t failure_site_capacity = 64U;
inline constexpr std::size_t failure_snapshot_message_capacity = 512U;

// The status a snapshot of the fail-fast boundary records: the C ABI's
// GF_STATUS_NATIVE_EXCEPTION (the ABI checks the two agree).
inline constexpr std::int32_t failure_native_exception_status = 16;

// The site a C ABI function's snapshot records.
inline constexpr std::string_view c_abi_failure_site = "c_abi";

// What the failing boundary was and what it saw. status is zero when the
// thread has no snapshot. interface_id and function_index name the refusing
// C ABI function (its table id and slot index), zero for a callback entry.
// depth counts the frames open where the exception was thrown; frame_count
// (at most shadow_stack_capacity) the recorded ones, the leading elements of
// frames. The text arrays hold site_size and message_size bytes; the bytes
// past them are zero.
struct GUI_FORMS_API FailureSnapshot final {
    std::int32_t status = 0;
    std::uint32_t interface_id = 0U;
    std::uint32_t function_index = 0U;
    std::uint64_t sequence = 0U;
    std::uint32_t frame_count = 0U;
    std::uint32_t depth = 0U;
    std::uint32_t site_size = 0U;
    std::uint32_t message_size = 0U;
    std::array<char, failure_site_capacity> site{};
    std::array<char, failure_snapshot_message_capacity> message{};
    std::array<ShadowFrame, shadow_stack_capacity> frames{};
};

// Where a snapshot is taken: the boundary's site and status, and the C ABI
// function when there is one (else zero).
struct GUI_FORMS_API FailureBoundary final {
    std::string_view site;
    std::int32_t status = 0;
    std::uint32_t interface_id = 0U;
    std::uint32_t function_index = 0U;
};

// Replaces the calling thread's snapshot with message at boundary and the
// frames of shadow, the shadow record a gate attached to the failure's
// ErrorRecord (no frames when it is not present). Any time after the gate
// returned, on the gate's thread, while the record is among the thread's
// last stxlib::shadow_record_capacity. status must not be zero.
GUI_FORMS_API void record_failure_snapshot(const FailureBoundary& boundary, std::string_view message,
                                           stxlib::ShadowReference shadow) noexcept;

// Replaces the calling thread's snapshot with message at boundary and the
// failure's shadow frames, for a handler that is not a gate: to be called
// inside the catch block that caught the exception (it takes the thread's
// pending capture, copy_failure_shadow_frames). status must not be zero.
GUI_FORMS_API void record_failure_snapshot(const FailureBoundary& boundary, std::string_view message) noexcept;

// As record_failure_snapshot, with failure's text as the message (what() for
// a std::exception, else a fixed text).
GUI_FORMS_API void record_failure_snapshot(const FailureBoundary& boundary, std::exception_ptr failure) noexcept;

// The calling thread's snapshot, borrowed for use on this thread until its
// next record or clear; a C ABI reader copies out of it.
[[nodiscard]] GUI_FORMS_API const FailureSnapshot& thread_failure_snapshot() noexcept;

// Forgets the calling thread's snapshot (status zero, no frames, no text);
// sequence keeps counting.
GUI_FORMS_API void clear_failure_snapshot() noexcept;

// Exception reflection's process switch. Any thread.
GUI_FORMS_API void set_exception_reflection(bool enabled) noexcept;
[[nodiscard]] GUI_FORMS_API bool exception_reflection() noexcept;

} // namespace gui_forms::detail
