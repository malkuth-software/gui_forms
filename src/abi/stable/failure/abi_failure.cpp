#include "abi_failure.hpp"

#include <cstring>

namespace gui_forms::abi1 {

namespace {

thread_local FailureRecord current_thread_failure;

// A UTF-8 continuation byte has the bit pattern 10xxxxxx.
bool is_continuation_byte(char byte) noexcept {
    const unsigned char value = static_cast<unsigned char>(byte);
    return (value & 0xc0U) == 0x80U;
}

} // namespace

FailureRecord& thread_failure() noexcept {
    return current_thread_failure;
}

std::size_t utf8_prefix_length(std::string_view text, std::size_t capacity) noexcept {
    if (text.size() <= capacity) {
        return text.size();
    }
    // text[capacity] is the first byte left out. Step back while it would
    // split a sequence: a cut is whole when the byte after it is not a
    // continuation byte.
    std::size_t length = capacity;
    while (length > 0U && is_continuation_byte(text[length])) {
        --length;
    }
    return length;
}

gf_status fail(gf_status status, SlotIdentity slot, std::string_view message) noexcept {
    FailureRecord& record = current_thread_failure;
    const std::size_t length = utf8_prefix_length(message, record.message.size());
    record.status = status;
    record.slot = slot;
    record.message_size = static_cast<std::uint32_t>(length);
    if (length != 0U) {
        std::memcpy(record.message.data(), message.data(), length);
    }
    return status;
}

void clear_failure() noexcept {
    FailureRecord& record = current_thread_failure;
    record.status = GF_STATUS_OK;
    record.slot = SlotIdentity{};
    record.message_size = 0U;
}

} // namespace gui_forms::abi1
