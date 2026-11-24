#include "prepared_window_batch.hpp"
#include <limits>
#include <new>
#include <stxlib/threads.hpp>

namespace gui_forms::detail {
struct PreparedWindowBatchAccess final {
    static const std::shared_ptr<PreparedLedger>& ledger(const PreparedWindowInput& input) noexcept {
        return input.ledger_;
    }
    static void transfer(PreparedWindowInput& input, PreparedWindowBatchStorage& output) noexcept {
        output.input = std::move(input.data_);
    }
    static void release_input_charge(PreparedWindowInput& input) noexcept {
        input.ledger_.reset();
        input.reservation_.release();
    }
};
namespace {
PreparedTextStatus current_locked(const PreparedWindowBatchAuthority& authority,
                                 const PreparedWindowKey& expected) noexcept {
    if (authority.closing) return PreparedTextStatus::closing;
    if (authority.executor != stxlib::current_thread()) return PreparedTextStatus::wrong_executor;
    if (!authority.desired || !same_prepared_window_key(*authority.desired, expected))
        return PreparedTextStatus::stale;
    return PreparedTextStatus::success;
}
}
PreparedTextStatus desire_prepared_window(PreparedWindowBatchAuthority& authority,
                                         const PreparedWindowKey& key) {
    const stxlib::ScopedLock lock(authority.mutex);
    if (authority.closing) return PreparedTextStatus::closing;
    if (authority.executor != stxlib::current_thread()) return PreparedTextStatus::wrong_executor;
    if (!key.controller_instance || !key.projection_generation || !key.authority.session || !key.authority.epoch)
        return PreparedTextStatus::invalid_input;
    const PreparedTextStatus valid = validate_prepared_key(key.text);
    if (valid != PreparedTextStatus::success) return valid;
    if (authority.last_epoch) {
        if (authority.last_epoch == std::numeric_limits<std::uint64_t>::max())
            return PreparedTextStatus::generation_exhausted;
        if (key.controller_instance != authority.controller_instance || key.authority.session != authority.session)
            return PreparedTextStatus::invalid_input;
        if (key.authority.epoch <= authority.last_epoch) return PreparedTextStatus::stale;
    }
    authority.controller_instance = key.controller_instance;
    authority.session = key.authority.session;
    authority.last_epoch = key.authority.epoch;
    authority.desired = key;
    return PreparedTextStatus::success;
}
PreparedTextStatus admit_prepared_window(const PreparedWindowKey& expected,
    std::shared_ptr<PreparedWindowBatchAuthority> authority, std::shared_ptr<PreparedLedger> ledger,
    std::shared_ptr<const PreparedFontBank> fonts, PreparedWindowInput& input,
    std::unique_ptr<PreparedWindowBatchStorage>& output) {
    if (output) return PreparedTextStatus::busy;
    if (!input || !authority || !ledger || !fonts) return PreparedTextStatus::invalid_input;
    if (PreparedWindowBatchAccess::ledger(input) != ledger || (*fonts).ledger != ledger)
        return PreparedTextStatus::invalid_input;
    const PreparedWindowInputData& data = input.data();
    if (!same_prepared_window_key(data.key, expected)) return PreparedTextStatus::stale;
    if ((*fonts).identity != expected.text.font_set || (*fonts).generation != expected.text.font_generation)
        return PreparedTextStatus::incompatible_font;
    {
        const stxlib::ScopedLock lock((*authority).mutex);
        const PreparedTextStatus current = current_locked(*authority, expected);
        if (current != PreparedTextStatus::success) return current;
    }
    // Counts and source certification were frozen by own_prepared_window.
    // Check subtraction before multiplication even though its row cap is 512.
    constexpr std::size_t limit = PreparedTextLimits::payload_bytes;
    if (data.charged_bytes > limit - sizeof(PreparedWindowBatchStorage))
        return PreparedTextStatus::budget_exceeded;
    const std::size_t base = data.charged_bytes + sizeof(PreparedWindowBatchStorage);
    if (data.paragraph_count > (limit - base) / sizeof(PreparedWindowRowStorage))
        return PreparedTextStatus::budget_exceeded;
    const std::size_t bytes = base + data.paragraph_count * sizeof(PreparedWindowRowStorage);
    PreparedReservation reservation{};
    const PreparedTextStatus reserved = reservation.acquire(ledger, PreparedResource::payload, limit);
    if (reserved != PreparedTextStatus::success) return reserved;
    try {
        std::unique_ptr<PreparedWindowBatchStorage> next = std::make_unique<PreparedWindowBatchStorage>();
        std::unique_ptr<PreparedWindowRowStorage[]> rows = std::make_unique<PreparedWindowRowStorage[]>(data.paragraph_count);
        for (std::size_t index = 0; index < data.paragraph_count; ++index)
            rows[index].paragraph_index = index;
        (*next).reservation = std::move(reservation);
        (*next).ledger = ledger;
        (*next).authority = authority;
        (*next).fonts = std::move(fonts);
        (*next).key = expected;
        (*next).rows = std::move(rows);
        (*next).row_count = data.paragraph_count;
        (*next).requested_bytes = bytes;
        const stxlib::ScopedLock authority_lock((*authority).mutex);
        const PreparedTextStatus current = current_locked(*authority, expected);
        if (current != PreparedTextStatus::success) return current;
        {
            // Lock order is authority, then ledger. Reservation acquisition
            // above holds only ledger; no callback or allocation occurs here.
            const stxlib::ScopedLock ledger_lock((*ledger).mutex);
            if ((*ledger).closing) return PreparedTextStatus::closing;
            PreparedWindowBatchAccess::transfer(input, *next);
        }
        // Release acquires ledger itself, after the commit lock is gone.
        PreparedWindowBatchAccess::release_input_charge(input);
        output = std::move(next);
    } catch (const std::bad_alloc&) {
        return PreparedTextStatus::resource_failure;
    }
    return PreparedTextStatus::success;
}
bool prepared_window_batch_current(const PreparedWindowBatchStorage& batch) {
    if (!batch.authority || !batch.ledger) return false;
    const stxlib::ScopedLock authority_lock((*batch.authority).mutex);
    const PreparedTextStatus current = current_locked(*batch.authority, batch.key);
    if (current != PreparedTextStatus::success) return false;
    const stxlib::ScopedLock ledger_lock((*batch.ledger).mutex);
    const bool live = !(*batch.ledger).closing;
    return live;
}
PreparedTextStatus prepared_window_worker_current_locked(const PreparedWindowBatchStorage& batch) noexcept {
    if (!batch.authority || !batch.ledger) return PreparedTextStatus::invalid_input;
    const PreparedWindowBatchAuthority& authority = *batch.authority;
    if (authority.closing || (*batch.ledger).closing) return PreparedTextStatus::closing;
    if (!authority.desired || !same_prepared_window_key(*authority.desired, batch.key))
        return PreparedTextStatus::stale;
    return PreparedTextStatus::success;
}
PreparedTextStatus prepared_window_worker_current(const PreparedWindowBatchStorage& batch) {
    if (!batch.authority || !batch.ledger) return PreparedTextStatus::invalid_input;
    const stxlib::ScopedLock authority_lock((*batch.authority).mutex);
    const stxlib::ScopedLock ledger_lock((*batch.ledger).mutex);
    const PreparedTextStatus status = prepared_window_worker_current_locked(batch);
    return status;
}
}
