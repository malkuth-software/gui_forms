#pragma once

// Output records of the C ABI (major 1): the caller sets struct_size to the
// size it was compiled with, at least the record's 1.0 size; the library
// writes the fields it knows after struct_size and leaves struct_size, and
// any later bytes of a larger record, as they were.

#include "gui_forms/c/gf_abi.h"

#include "../failure/abi_failure.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace gui_forms::abi1 {

// An output record the caller sized for at least the 1.0 fields.
template <typename Record>
gf_status check_record(const Record* record, SlotIdentity slot) {
    if (record == nullptr || (*record).struct_size < sizeof(Record)) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "the output record is null or smaller than its 1.0 size");
    }
    return GF_STATUS_OK;
}

// Writes the 1.0 fields after struct_size; a larger caller record keeps its
// later bytes, which this library does not know.
template <typename Record>
void write_record(const Record& complete, Record* record) noexcept {
    const std::size_t skipped = sizeof(std::uint32_t);
    unsigned char* destination = reinterpret_cast<unsigned char*>(record);
    const unsigned char* source = reinterpret_cast<const unsigned char*>(&complete);
    std::memcpy(destination + skipped, source + skipped, sizeof(Record) - skipped);
}

} // namespace gui_forms::abi1
