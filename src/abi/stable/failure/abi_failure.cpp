#include "abi_failure.hpp"

#include <stxlib/shadow.hpp>

#include <cstring>
#include <exception>
#include <memory>
#include <new>
#include <utility>

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

stxlib::ShadowReference ParkedFailure::park() noexcept {
    const stxlib::ShadowReference parked = stxlib::park_shadow();
    if (!exception) {
        exception = std::current_exception();
        shadow = parked;
        // Copied now, before any other capture on the thread can overwrite
        // the record. A refused allocation keeps the reference alone.
        frames.reset(new (std::nothrow) detail::ParkedShadowCopy());
        if (frames) {
            detail::copy_parked_shadow(parked, *frames);
        }
    }
    return parked;
}

void ParkedFailure::rethrow() {
    if (!exception) {
        return;
    }
    const std::exception_ptr carried = exception;
    const stxlib::ShadowReference carried_shadow = shadow;
    std::unique_ptr<detail::ParkedShadowCopy> carried_frames = std::move(frames);
    exception = nullptr;
    shadow = stxlib::ShadowReference{};
    if (carried_frames) {
        detail::carry_parked_shadow(*carried_frames);
    }
    carried_frames.reset();
    stxlib::restore_shadow(carried_shadow);
    std::rethrow_exception(carried);
}

} // namespace gui_forms::abi1
