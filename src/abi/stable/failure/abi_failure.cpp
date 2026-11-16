#include "abi_failure.hpp"

#include "../../../core/failure/failure_snapshot.hpp"
#include "../../../platform/stxlib_bridge.hpp"

#include <stxlib/result.hpp>
#include <stxlib/shadow.hpp>

#include <cstring>
#include <exception>
#include <memory>
#include <new>
#include <span>
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

namespace {

// refuse_next_parked_copy's switch. Trivially destructible, as a
// thread_local must be (owner rule 2).
thread_local bool refuse_parked_copy = false;

} // namespace

void refuse_next_parked_copy() noexcept {
    refuse_parked_copy = true;
}

stxlib::ShadowReference ParkedFailure::park() noexcept {
    if (exception) {
        const stxlib::ShadowReference later = stxlib::park_shadow();
        return later;
    }
    exception = std::current_exception();
    // Copied now, before any other capture on the thread can overwrite the
    // record. The rooms are the stated sizes, so park_shadow_into fails only
    // for an exhausted sequence; then, or when the allocation is refused,
    // the reference alone is kept.
    std::unique_ptr<ParkedShadowFrames> copy;
    if (refuse_parked_copy) {
        refuse_parked_copy = false;
    } else {
        copy.reset(new (std::nothrow) ParkedShadowFrames());
    }
    if (copy) {
        const stxlib::OperationResult<stxlib::ParkedShadow> parked = stxlib::park_shadow_into(
            platform::stxlib_mutable_view((*copy).records), platform::stxlib_mutable_view((*copy).trace));
        if (parked.succeeded()) {
            (*copy).parked = *parked.value();
            shadow = (*copy).parked.reference;
            frames = std::move(copy);
            return shadow;
        }
        copy.reset();
    }
    shadow = stxlib::park_shadow();
    frames_lost = true;
    detail::count_parked_frames_dropped();
    return shadow;
}

void ParkedFailure::rethrow() {
    if (!exception) {
        return;
    }
    const std::exception_ptr carried = exception;
    const stxlib::ShadowReference carried_shadow = shadow;
    std::unique_ptr<ParkedShadowFrames> carried_frames = std::move(frames);
    const bool carried_lost = frames_lost;
    exception = nullptr;
    shadow = stxlib::ShadowReference{};
    frames_lost = false;
    if (carried_frames) {
        const ParkedShadowFrames& copy = *carried_frames;
        const std::span<const stxlib::ShadowFrameRecord> records(copy.records.data(), copy.parked.frame_count);
        const std::span<const stxlib::uptr> trace(copy.trace.data(), copy.parked.trace_count);
        // The new record's reference is not needed: it is pending, and the
        // outer gate's record names it.
        static_cast<void>(stxlib::restore_shadow_from(copy.parked, platform::stxlib_view(records),
                                                      platform::stxlib_view(trace)));
    } else {
        if (carried_lost) {
            detail::mark_parked_frames_lost(carried_shadow);
        }
        stxlib::restore_shadow(carried_shadow);
    }
    carried_frames.reset();
    std::rethrow_exception(carried);
}

} // namespace gui_forms::abi1
