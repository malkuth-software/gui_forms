#pragma once

#include "gui_forms/c/gf_abi.h"

#include <array>
#include <cstdint>
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

} // namespace gui_forms::abi1
