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

// -- caller arrays of size-prefixed records ----------------------------------

// Element index of a caller's array whose stride is its first element's
// struct_size (the caller's sizeof, so a record that grows in a later minor
// is still stepped correctly). The stride is a u32 and every caller bounds
// the count (at most 4096 collection items, 8 rule actions or conditions,
// 17 packages, GF_DATA_GRID_MAXIMUM_ROWS_PER_CALL rows), so the offset
// cannot overflow.
template <typename Record>
const Record& element_at(const Record* first, std::uint32_t stride, std::uint64_t index) noexcept {
    const unsigned char* base = reinterpret_cast<const unsigned char*>(first);
    const std::uint64_t offset = static_cast<std::uint64_t>(stride) * index;
    const Record* element = reinterpret_cast<const Record*>(base + offset);
    return *element;
}

template <typename Record>
Record& mutable_element_at(Record* first, std::uint32_t stride, std::uint64_t index) noexcept {
    unsigned char* base = reinterpret_cast<unsigned char*>(first);
    const std::uint64_t offset = static_cast<std::uint64_t>(stride) * index;
    Record* element = reinterpret_cast<Record*>(base + offset);
    return *element;
}

// Checks a caller's array of count records, validated whole before any is
// read: first may be null only for none, and every element repeats the
// first one's struct_size, at least minimum and a multiple of 8. stride
// receives it.
template <typename Record>
gf_status check_array(const Record* first, std::uint64_t count, std::uint32_t minimum, SlotIdentity slot,
                      std::uint32_t& stride) {
    if (count == 0U) {
        stride = minimum;
        return GF_STATUS_OK;
    }
    if (first == nullptr) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "an array with elements needs its first element");
    }
    const std::uint32_t declared = (*first).struct_size;
    if (declared < minimum || declared % 8U != 0U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot,
                    "an array's stride is its first element's struct_size, at least the record's size");
    }
    for (std::uint64_t index = 1U; index < count; ++index) {
        if (element_at(first, declared, index).struct_size != declared) {
            return fail(GF_STATUS_INVALID_ARGUMENT, slot, "every element of an array repeats its struct_size");
        }
    }
    stride = declared;
    return GF_STATUS_OK;
}

} // namespace gui_forms::abi1
