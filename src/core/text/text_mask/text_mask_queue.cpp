#include "text_mask_state.hpp"
#include "../../thread_records/core_thread_record.hpp"
#include <cstring>

namespace gui_forms::detail {
namespace {
bool same_options(const MaskOptions& left, const MaskOptions& right) noexcept {
    const bool equal = left.primary_face == right.primary_face && left.size_64 == right.size_64 &&
        left.width_64 == right.width_64 && left.gap_64 == right.gap_64 && left.scale == right.scale && left.raster == right.raster;
    return equal;
}
void touch_cache(std::array<MaskCacheEntry, 700>& cache, const std::size_t selected) noexcept {
    const std::size_t previous = cache[selected].rank;
    for (std::size_t index = 0; index < cache.size(); ++index) {
        MaskCacheEntry& entry = cache[index];
        if (entry.value && entry.rank < previous) ++entry.rank;
    }
    cache[selected].rank = 0;
}
}

void MaskSlot::retire() noexcept {
    result.reset();
    key.reset();
    charge.reset();
    id = {};
    completion = {};
    state = TextMaskSlot::empty;
}
std::shared_ptr<const MaskFontOwner> TextMaskSessionState::find_bank(const EncodedFontLease& fonts) const {
    const std::shared_ptr<const PreparedFontBank>& candidate = PreparedTextAccess::fonts(fonts);
    if (!candidate) return {};
    for (std::size_t index = 0; index < banks.size(); ++index) {
        std::shared_ptr<const MaskFontOwner> bank = banks[index].lock();
        if (bank && &(*bank).bank == candidate.get()) return bank;
    }
    return {};
}
std::shared_ptr<const TextMaskStorage> TextMaskSessionState::cache_lookup_locked(
    const MaskFontOwner& fonts, const MaskOptions& options, const std::string_view text) {
    for (std::size_t index = 0; index < cache.size(); ++index) {
        const std::shared_ptr<const TextMaskStorage>& value = cache[index].value;
        if (!value) continue;
        const MaskKey& key = *(*value).key;
        if (key.fonts.get() != &fonts || !same_options(key.options, options) || key.text.size() != text.size()) continue;
        if (!text.empty() && std::memcmp(key.text.data(), text.data(), text.size()) != 0) continue;
        touch_cache(cache, index);
        return value;
    }
    return {};
}
void TextMaskSessionState::cache_insert_locked(const std::shared_ptr<const TextMaskStorage>& value) {
    const MaskKey& key = *(*value).key;
    std::string_view text{};
    if (!key.text.empty()) text = std::string_view(key.text.data(), key.text.size());
    const std::shared_ptr<const TextMaskStorage> existing = cache_lookup_locked(*key.fonts, key.options, text);
    if (existing) return;
    std::size_t selected = 0;
    for (std::size_t index = 0; index < cache.size(); ++index) {
        if (!cache[index].value) { selected = index; break; }
        if (cache[index].rank > cache[selected].rank) selected = index;
    }
    MaskCacheEntry& target = cache[selected];
    if (!target.value) target.charge.acquire(ledger, MaskResource::cache, 1);
    target.value = value;
    // Existing ranks are at most 699. Exclude replacement before incrementing.
    for (std::size_t index = 0; index < cache.size(); ++index) {
        if (index != selected && cache[index].value) ++cache[index].rank;
    }
    target.rank = 0;
}
void TextMaskSessionState::clear_cache_locked() noexcept {
    for (std::size_t index = 0; index < cache.size(); ++index) {
        cache[index].value.reset();
        cache[index].charge.reset();
        cache[index].rank = 0;
    }
}
void TextMaskSessionState::close() {
    std::lock_guard<std::mutex> lock(mutex);
    closing = true;
    wake_requested = false;
    clear_cache_locked();
    for (std::size_t index = 0; index < slots.size(); ++index) {
        MaskSlot& slot = slots[index];
        if (slot.state == TextMaskSlot::running || slot.state == TextMaskSlot::retiring) slot.state = TextMaskSlot::retiring;
        else slot.retire();
    }
    condition.notify_all();
}
void TextMaskSessionState::join() {
    close();
    if (worker.joinable()) worker.join();
    std::lock_guard<std::mutex> lock(mutex);
    wake = nullptr;
    backend = nullptr;
    joined = true;
}
TextMaskSessionState::~TextMaskSessionState() { join(); }

void TextMaskSessionState::run() noexcept {
    // Closes last: the thread's records are released here, on the thread.
    const gui_forms::detail::ThreadRecordsEnd thread_records_end;
    for (;;) {
        std::size_t selected = slots.size();
        std::shared_ptr<const MaskKey> key{};
        PreparedTextWakeTarget* notification = nullptr;
        bool wake_only = false;
        {
            std::unique_lock<std::mutex> lock(mutex);
            for (;;) {
                if (closing) return;
                if (wake_requested) {
                    wake_requested = false;
                    notification = wake;
                    wake_only = true;
                    break;
                }
                for (std::size_t index = 0; index < slots.size(); ++index) {
                    const MaskSlot& slot = slots[index];
                    if (slot.state != TextMaskSlot::dispatched && slot.state != TextMaskSlot::queued) continue;
                    if (selected == slots.size() || slot.id.serial < slots[selected].id.serial) selected = index;
                }
                if (selected != slots.size()) break;
                condition.wait(lock);
            }
            if (!wake_only) {
                for (std::size_t index = 0; index < slots.size(); ++index) {
                    if (slots[index].state == TextMaskSlot::dispatched) slots[index].state = TextMaskSlot::queued;
                }
                slots[selected].state = TextMaskSlot::running;
                key = slots[selected].key;
            }
        }
        if (wake_only) {
            if (notification != nullptr) (*notification).post_prepared_text_wake();
            continue;
        }
        TextMaskResult result{TextMaskStatus::unsupported_profile};
        std::shared_ptr<const TextMaskStorage> candidate{};
        try {
            if (backend != nullptr) result = (*backend).execute(ledger, key, candidate);
            if (result.status == TextMaskStatus::success && (!candidate || (*candidate).key != key)) {
                result = {TextMaskStatus::native_failure};
            }
        } catch (const MaskBudgetFailure& failure) { result = mask_budget_result(failure.resource); }
        catch (const MaskLimitFailure& failure) { result = {TextMaskStatus::limit_exceeded, failure.limit}; }
        catch (const std::bad_alloc&) { result = {TextMaskStatus::resource_failure}; }
        catch (...) { result = {TextMaskStatus::native_failure}; }
        {
            std::lock_guard<std::mutex> lock(mutex);
            MaskSlot& slot = slots[selected];
            if (closing || slot.state == TextMaskSlot::retiring) {
                // Drop all call-owned references before freeing the charged slot.
                candidate.reset();
                key.reset();
                slot.retire();
                if (!closing && !wake_pending) { wake_pending = true; notification = wake; }
            } else {
                if (result.status == TextMaskStatus::success) {
                    slot.result = candidate;
                    try { cache_insert_locked(candidate); }
                    catch (...) { /* Caching is optional; completed ownership survives. */ }
                }
                candidate.reset();
                key.reset();
                slot.completion = result;
                slot.state = TextMaskSlot::completed;
                if (!wake_pending) { wake_pending = true; notification = wake; }
            }
        }
        // close may revoke the slot after publication; the payload-free posted
        // wake can then observe closing. join waits until this borrow is idle.
        if (notification != nullptr) (*notification).post_prepared_text_wake();
    }
}
} // namespace gui_forms::detail
