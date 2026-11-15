#pragma once

#include "gui_forms/c/gf_abi.h"

#include <stxlib/error.hpp>
#include <stxlib/shadow.hpp>

#include <array>
#include <cstdint>
#include <exception>
#include <memory>
#include <string_view>

namespace gui_forms::abi1 {

// The function a failure belongs to: its table and the index of its function
// pointer in that table, counted from zero (GF_ABI_ENTRY_SLOT for
// gf_abi_get_table).
struct SlotIdentity {
    gf_table_id table_id = 0U;
    std::uint32_t slot_index = 0U;
};

// The longest message a failure keeps, in bytes. A longer message is cut at
// the last whole UTF-8 sequence that fits.
inline constexpr std::size_t failure_message_capacity = 512U;

// One thread's last failure. Each thread owns its own record (thread_local
// storage, defined in abi_failure.cpp); no record is read or written by
// another thread. The record is fixed-size, so recording a failure never
// allocates and cannot fail.
struct FailureRecord {
    gf_status status = GF_STATUS_OK;
    SlotIdentity slot;
    std::uint32_t message_size = 0U;
    std::array<char, failure_message_capacity> message{};
};

// The calling thread's record. The reference stays valid for the thread's
// lifetime; it is never handed to a caller of the ABI.
[[nodiscard]] FailureRecord& thread_failure() noexcept;

// Replaces the calling thread's record with this failure and returns status,
// so a slot can write `return fail(...)`. status must not be GF_STATUS_OK.
gf_status fail(gf_status status, SlotIdentity slot, std::string_view message) noexcept;

// Forgets the calling thread's failure.
void clear_failure() noexcept;

// The number of leading bytes of text that end on a whole UTF-8 sequence and
// fit in capacity. Text that is not valid UTF-8 is cut at the last lead byte
// boundary, never inside a sequence.
[[nodiscard]] std::size_t utf8_prefix_length(std::string_view text, std::size_t capacity) noexcept;

// A C++ exception carried as a value and rethrown later, with the shadow
// record of its throw (survey B8; the class bridge's frames, HookFrame). A
// base function catches the failure of the raiser below inside its slot
// gate, so the C entry that called base returns normally; once the entry
// has returned, the raise rethrows the exception, and the outer gate that
// catches it files the frames of the original throw, not of the rethrow.
// One per carrying frame, on its thread.
//
// The failure owns its frames (the owner's decision for S3, the A2 list's
// G3): the park copies the record's frames and trace into frames
// (stxlib::park_shadow_into), so they survive any capture between the park
// and the rethrow (a thread keeps the last 4 records in a development build,
// the last 1 in a release build), and a nested park keeps its own. The
// rethrow files the copy as a new record and makes it pending
// (stxlib::restore_shadow_from), so the outer gate, STXlib's record and
// GUI.Forms' snapshot alike, attaches the original throw's frames.
//
// The copy (about 7.9 KiB) is allocated on the failure path, not kept in
// every HookFrame on a class entry's stack. When the allocation is refused,
// or STXlib cannot park into it (an exhausted shadow sequence), the
// reference alone is kept (stxlib::park_shadow and restore_shadow): its
// frames then survive only while the thread still holds the record, and the
// outer gate files none otherwise (detected, never another failure's).
struct ParkedShadowFrames final {
    stxlib::ParkedShadow parked{};
    std::array<stxlib::ShadowFrameRecord, stxlib::parked_shadow_frame_room> records{};
    std::array<stxlib::uptr, stxlib::parked_shadow_trace_room> trace{};
};

struct ParkedFailure final {
    std::exception_ptr exception;
    // The parked record (frames' parked.reference when the copy is held).
    stxlib::ShadowReference shadow{};
    // The copy of shadow's frames and trace; null when nothing is kept or
    // only the reference is.
    std::unique_ptr<ParkedShadowFrames> frames;

    // Inside a catch block: parks the current exception's shadow record
    // (which clears the thread's pending capture) and returns it, for the
    // failure's snapshot, which reads it at once. The first failure parked
    // is the one kept, with its copy of the record; a later one is only
    // parked (stxlib::park_shadow) and returned.
    stxlib::ShadowReference park() noexcept;

    // Rethrows the kept exception after filing its copy of the frames as
    // the thread's pending capture (stxlib::restore_shadow_from; with only
    // the reference, stxlib::restore_shadow), and holds nothing afterwards.
    // Returns without throwing when nothing is kept.
    void rethrow();
};

} // namespace gui_forms::abi1
